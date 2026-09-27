/*
 *   koii.c
 *   Copyright (C) 2027 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Elektroid.
 *
 *   Elektroid is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Elektroid is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Elektroid. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "common.h"

enum teep_fs
{
  FS_TEEP_SAMPLE
};

struct teep_iterator_data
{
  guint next;
  guint bank;
  struct backend *backend;
};

static const guint8 FAMILY_ID[] = { 0x20, 0x0 };
static const guint8 EP_133_ID[] = { 0x02, 0x0 };

static const guint8 TEEP_REQ_HEADER[] =
  { 0xf0, 0, 0x20, 0x76, 0x33, 0x40, 0, 0, 0 };

#define TEEP_DIRS_LEN 10
#define TEEP_DIR_SAMPLE_LEN 100

// Sequence numbers belong to [ 0x1000, 0x1fff ].
// TX sequence number is sent as (TEEP_SEQ_TX_BIT | seq) and belong to [ 0x3000, 0x3fff ].
// RX is just the sequence number.
// A special value of 0x2000 is sent from the device when is not following a response. These messages should be skipped entirely.
// Notice these will appear as 2 MIDI bytes (only 7 bits).
#define TEEP_SEQ_MAX (0x40 << 7 | 0x00)
#define TEEP_SEQ_TX_BIT 0x2000
#define TEEP_SEQ_START (0x20 << 7 | 0x00)	// Lower values make the device to not reply with the sequence.
#define TEEP_SEQ_NO_REPLY 0x2000

#define TEEP_SAMPLE_PACKET_PAYLOAD (326 - 2)	// A partial payload is preceded by 2 bytes indicating the part.

#define TEEP_SAMPLERATE 46875

static const guint8 TEEP_HANDSHAKE_MSG[] = { 1, 1, 0, 0x40, 0, 0 };

static const guint8 TEEP_SIZE_MSG[] = { 4, 0, 0, 0, 0 };

static const guint8 TEEP_SAMPLE_METADATA_MSG[] = { 7, 2, 0, 0, 0, 0 };

static const guint8 TEEP_SAMPLE_PLAYBACK_MSG[] =
  { 5, 1, 0, 0, 0, 0, 0, 0, 0, 0x80, 3, 0x68 };

static const guint8 TEEP_SAMPLE_DELETE_MSG[] = { 6, 0, 0 };

static const guint8 TEEP_SAMPLE_OPEN_MSG[] = { 3, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 TEEP_SAMPLE_PART_MSG[] = { 3, 1, 0, 0 };

static const gchar *TEEP_DIRS[] =
  { "KICK", "SNARE", "CYMB", "PERC", "BASS", "MELOD", "LOOP", "USER 1",
  "USER 2", "SFX"
};

static GByteArray *
teep_get_msg (const guint8 *payload, guint len)
{
  GByteArray *msg = g_byte_array_new ();
  g_byte_array_append (msg, payload, len);
  return msg;
}

static GByteArray *
teep_get_raw_msg (struct backend *backend, GByteArray *cmd_8bit,
		  guint16 *seq_tx)
{
  guint16 seqv;
  guint8 *cmd_midi;
  guint cmd_midi_len;
  guint16 *seq = backend->data;
  GByteArray *msg = g_byte_array_sized_new (256);
  g_byte_array_append (msg, TEEP_REQ_HEADER, sizeof (TEEP_REQ_HEADER));

  *seq_tx = *seq;

  seqv = *seq;
  seqv |= TEEP_SEQ_TX_BIT;
  msg->data[6] = (seqv >> 7) & 0x7f;
  msg->data[7] = seqv & 0x7f;
  msg->data[8] = cmd_8bit ? 5 : 1;

  if (cmd_8bit)
    {
      cmd_midi_len = common_8bit_msg_to_midi_msg_size (cmd_8bit->len);
      cmd_midi = g_malloc (cmd_midi_len);
      common_8bit_msg_to_midi_msg (cmd_8bit->data, cmd_midi, cmd_8bit->len);
      g_byte_array_append (msg, cmd_midi, cmd_midi_len);
      g_free (cmd_midi);
    }
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  (*seq)++;
  if (*seq == TEEP_SEQ_MAX)
    {
      *seq = TEEP_SEQ_START;
    }

  return msg;
}

static GByteArray *
teep_tx_and_rx_sysex (struct backend *backend, GByteArray *tx_msg,
		      gint timeout)
{
  gint err;
  guint8 status;
  guint16 seq_tx, seq_rx;
  guint size_8bit, size_midi;
  GByteArray *midi_msg, *rx_msg, *payload;

  if (tx_msg)
    {
      if (debug_level)
	{
	  gchar *text = debug_get_hex_msg (tx_msg);
	  debug_print (1, "Payload sent (%d): %s", tx_msg->len, text);
	  g_free (text);
	}
    }
  else
    {
      debug_print (1, "Payload sent is empty");
    }

  midi_msg = teep_get_raw_msg (backend, tx_msg, &seq_tx);
  if (tx_msg)
    {
      free_msg (tx_msg);
    }
  err = backend_tx (backend, midi_msg);
  if (err)
    {
      return NULL;
    }
  while (1)
    {
      rx_msg = backend_rx (backend, -1, NULL);
      if (!rx_msg)
	{
	  return NULL;
	}

      seq_rx = rx_msg->data[7] | (rx_msg->data[6] << 7);
      if (seq_rx != TEEP_SEQ_NO_REPLY)
	{
	  break;
	}

      error_print ("Skipping no reply message...");
    }

  if (seq_tx != seq_rx)
    {
      error_print ("Unexpected sequence number (0x%04x -> 0x%04x)", seq_tx,
		   seq_rx);
    }

  status = rx_msg->data[9];
  size_midi = rx_msg->len - 11;
  size_8bit = common_midi_msg_to_8bit_msg_size (size_midi);
  payload = g_byte_array_sized_new (size_8bit);
  payload->len = size_8bit;
  common_midi_msg_to_8bit_msg (&rx_msg->data[10], payload->data, size_midi);
  free_msg (rx_msg);

  if (debug_level)
    {
      gchar *text = debug_get_hex_msg (payload);
      debug_print (1, "Payload received (%d): %s", payload->len, text);
      g_free (text);
    }

  if (status != 0)
    {
      debug_print (1, "Payload content (%d): %.*s", payload->len,
		   payload->len, payload->data);
      free_msg (payload);
      return NULL;
    }
  else
    {
      return payload;
    }
}

static GByteArray *
teep_sample_get_metadata_msg (guint32 id, guint32 part)
{
  GByteArray *msg = teep_get_msg (TEEP_SAMPLE_METADATA_MSG,
				  sizeof (TEEP_SAMPLE_METADATA_MSG));
  msg->data[2] = (id >> 8) & 0xff;
  msg->data[3] = id & 0xff;
  msg->data[4] = (part >> 8) & 0xff;
  msg->data[5] = part & 0xff;
  return msg;
}

static GByteArray *
teep_get_sample_metadata (struct backend *backend, guint32 id)
{
  gint err;
  guint part;
  guint8 last;
  GByteArray *tx_msg, *rx_msg;
  GByteArray *metadata = g_byte_array_sized_new (1024);

  err = 0;
  part = 0;
  while (1)
    {
      tx_msg = teep_sample_get_metadata_msg (id, part);
      rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  err = -EIO;
	  break;
	}

      // A partial payload is preceded by 2 bytes indicating the part.
      g_byte_array_append (metadata, &rx_msg->data[2], rx_msg->len - 2);
      last = rx_msg->data[1];
      debug_print (2, "Partial message: '%.*s'", metadata->len,
		   metadata->data);
      free_msg (rx_msg);

      if (last)
	{
	  break;
	}

      part++;
    }

  if (err)
    {
      free_msg (metadata);
      return NULL;
    }
  else
    {
      debug_print (2, "Message: '%s'", metadata->data);
      return metadata;
    }
}

static gint
teep_get_sample_info (struct backend *backend, guint32 id, gchar *name,
		      guint name_len, struct sample_info *sample_info)
{
  gint err;
  gint64 vs64;
  GError *error;
  const gchar *str;
  JsonReader *reader;
  JsonParser *parser;
  GByteArray *metadata = teep_get_sample_metadata (backend, id);

  if (!metadata)
    {
      return -EIO;
    }

  err = 0;
  error = NULL;
  parser = json_parser_new ();
  json_parser_load_from_data (parser, (gchar *) metadata->data, -1, &error);
  if (error)
    {
      error_print ("Error wile loading JSON: %s", error->message);
      g_error_free (error);
      err = -EIO;
    }
  else
    {
      reader = json_reader_new (json_parser_get_root (parser));

      if (json_reader_read_member (reader, "name"))
	{
	  g_snprintf (name, name_len, "%s",
		      json_reader_get_string_value (reader));
	  json_reader_end_member (reader);
	}
      else
	{
	  err = -EIO;
	}

      if (sample_info)
	{
	  sample_info->rate = TEEP_SAMPLERATE;
	  sample_info->format = SF_FORMAT_PCM_16;

	  if (json_reader_read_member (reader, "channels"))
	    {
	      sample_info->channels = json_reader_get_int_value (reader);
	      sample_info->frames =
		sample_info->frames / sample_info->channels;
	      json_reader_end_member (reader);

	    }
	  else
	    {
	      err = -EIO;
	    }

	  if (json_reader_read_member (reader, "sound.loopstart"))
	    {
	      vs64 = json_reader_get_int_value (reader);
	      sample_info->loop_start = vs64 > 0 ? vs64 : 0;
	      json_reader_end_member (reader);
	    }
	  else
	    {
	      sample_info->loop_start = 0;
	    }

	  if (json_reader_read_member (reader, "sound.loopend"))
	    {
	      vs64 = json_reader_get_int_value (reader);
	      sample_info->loop_end = vs64 > 0 ? vs64 :
		sample_info->frames - 1;
	      json_reader_end_member (reader);
	    }
	  else
	    {
	      sample_info->loop_end = sample_info->frames - 1;
	    }

	  sample_info->loop_type = 0;
	  if (json_reader_read_member (reader, "sound.playmode"))
	    {
	      str = json_reader_get_string_value (reader);
	      if (strcmp (str, "oneshot") == 0)
		{
		  // sample_info->loop_count = 1; // TODO
		}
	      else
		{
		  // sample_info->loop_count = 0; // TODO
		}
	      json_reader_end_member (reader);

	    }
	  else
	    {
	      //sample_info->loop_count = 1; // TODO
	    }

	  if (json_reader_read_member (reader, "sound.rootnote"))
	    {
	      sample_info->midi_note = json_reader_get_int_value (reader);
	      json_reader_end_member (reader);

	    }
	  else
	    {
	      sample_info->midi_note = 0;
	    }

	  // sample_info->midi_fraction; // TODO

	  // sample_info->beats; // TODO

	  if (json_reader_read_member (reader, "sound.bpm"))
	    {
	      sample_info->tempo = json_reader_get_double_value (reader);
	      json_reader_end_member (reader);

	    }
	  else
	    {
	      sample_info->tempo = 0;
	    }
	}

      g_object_unref (reader);
    }

  g_object_unref (parser);
  free_msg (metadata);

  return err;
}

static gint
teep_sample_next_dentry_type (struct item_iterator *iter)
{
  guint id;
  gchar name[LABEL_MAX];
  struct teep_iterator_data *data = iter->data;

  if (data->next < TEEP_DIR_SAMPLE_LEN)
    {
      id = data->bank * TEEP_DIR_SAMPLE_LEN + data->next;
      iter->item.type = ITEM_TYPE_FILE;
      iter->item.id = id;
      iter->item.size = -1;

      *name = 0;
      teep_get_sample_info (data->backend, id, name, LABEL_MAX, NULL);
      item_set_name (&iter->item, "%s", name);
      common_slot_set_slot_padded (&iter->item, 1);

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
teep_sample_next_dentry_root (struct item_iterator *iter)
{
  struct teep_iterator_data *data = iter->data;

  if (data->next < TEEP_DIRS_LEN)
    {
      iter->item.id = 0x10000 + data->next;
      iter->item.slot[0] = 0;
      item_set_name (&iter->item, "%s", TEEP_DIRS[data->next]);
      iter->item.type = ITEM_TYPE_DIR;
      iter->item.size = -1;

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
teep_get_dir_id_from_path (const gchar *path)
{
  const gchar *dir = &path[1];
  const gchar **names = TEEP_DIRS;
  for (guint i = 0; i < TEEP_DIRS_LEN; i++, names++)
    {
      if (!strcmp (dir, *names))
	{
	  return i;
	}
    }
  return -1;
}

static gint
teep_sample_read_dir (struct backend *backend, struct item_iterator *iter,
		      const gchar *dir, const gchar **extensions)
{
  if (!strcmp (dir, "/"))
    {
      struct teep_iterator_data *data =
	g_malloc (sizeof (struct teep_iterator_data));
      data->next = 0;
      data->bank = 0;
      data->backend = backend;
      item_iterator_init (iter, dir, data, teep_sample_next_dentry_root,
			  g_free);
      return 0;
    }
  else
    {
      gint bank = teep_get_dir_id_from_path (dir);
      if (bank < 0)
	{
	  return -ENOTDIR;
	}

      struct teep_iterator_data *data =
	g_malloc (sizeof (struct teep_iterator_data));
      data->next = bank == 0 ? 1 : 0;
      data->bank = bank;
      data->backend = backend;
      item_iterator_init (iter, dir, data, teep_sample_next_dentry_type,
			  g_free);

      return 0;
    }
}

static GByteArray *
teep_sample_delete_msg (guint id)
{
  GByteArray *msg = teep_get_msg (TEEP_SAMPLE_DELETE_MSG,
				  sizeof (TEEP_SAMPLE_DELETE_MSG));
  msg->data[1] = (id >> 8) & 0xff;
  msg->data[2] = id & 0xff;
  return msg;
}

static gint
teep_sample_delete (struct backend *backend, const gchar *path)
{
  gint err;
  guint id;
  GByteArray *tx_msg, *rx_msg;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  tx_msg = teep_sample_delete_msg (id);
  rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  free_msg (rx_msg);

  // TODO: complete

  return 0;
}

static GByteArray *
teep_sample_open_msg (guint id)
{
  GByteArray *msg = teep_get_msg (TEEP_SAMPLE_OPEN_MSG,
				  sizeof (TEEP_SAMPLE_OPEN_MSG));
  msg->data[2] = (id >> 8) & 0xff;
  msg->data[3] = id & 0xff;
  return msg;
}

static GByteArray *
teep_sample_get_part_msg (guint32 part)
{
  GByteArray *msg = teep_get_msg (TEEP_SAMPLE_PART_MSG,
				  sizeof (TEEP_SAMPLE_PART_MSG));
  msg->data[2] = (part >> 8) & 0xff;
  msg->data[3] = part & 0xff;
  return msg;
}

static gint
teep_sample_download (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;
  gboolean active;
  gchar name[LABEL_MAX];
  guint32 size, packets;
  struct sample_info *sample_info;
  GByteArray *tx_msg, *rx_msg, *data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  tx_msg = teep_sample_open_msg (id);
  rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  size = ((guint32 *) rx_msg->data)[1];
  size = GINT32_FROM_BE (size) >> 8;
  free_msg (rx_msg);

  data = g_byte_array_sized_new (size);

  packets = ceil (size / (double) TEEP_SAMPLE_PACKET_PAYLOAD);
  task_control_reset (control, packets);

  for (guint32 i = 0; i < packets; i++)
    {
      tx_msg = teep_sample_get_part_msg (i);
      rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  free_msg (data);
	  return -EIO;
	}

      g_byte_array_append (data, &rx_msg->data[2], rx_msg->len - 2);
      free_msg (rx_msg);

      task_control_set_progress (control, 1.0);
      control->part++;

      active = controllable_is_active (&control->controllable);
      if (!active)
	{
	  free_msg (data);
	  return -EIO;
	}
    }

  sample_info = sample_info_new (FALSE);
  // This is a bit of a hack.
  // We pass the samples here and we'll convert it to frames in teep_get_sample_info.
  sample_info->frames = size / 2;
  err = teep_get_sample_info (backend, id, name, LABEL_MAX, sample_info);
  if (err)
    {
      free_msg (data);
      sample_info_free (sample_info);
    }
  else
    {
      idata_init (sample, data, g_strdup (name), sample_info,
		  sample_info_free);
    }

  return err;
}

static gint
teep_sample_save (struct backend *backend, const gchar *path,
		  struct idata *sample, struct task_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static GByteArray *
teep_sample_get_playback_msg (guint id)
{
  GByteArray *msg = teep_get_msg (TEEP_SAMPLE_PLAYBACK_MSG,
				  sizeof (TEEP_SAMPLE_PLAYBACK_MSG));
  msg->data[2] = (id >> 8) & 0xff;
  msg->data[3] = id & 0xff;
  return msg;
}

static void
teep_sample_select (struct backend *backend, const gchar *dir,
		    struct item *item)
{
  GByteArray *tx_msg, *rx_msg;

  if (item->id >= TEEP_DIRS_LEN * TEEP_DIR_SAMPLE_LEN)
    {
      return;
    }

  tx_msg = teep_sample_get_playback_msg (item->id);
  rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
  if (rx_msg)
    {
      free_msg (rx_msg);
    }
  else
    {
      debug_print (1, "Error while playing sample with id %d", item->id);
    }
}

static const struct fs_operations FS_TEEP_SAMPLE_OPERATIONS = {
  .id = FS_TEEP_SAMPLE,
  .options =
    FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_STEREO |
    FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = teep_sample_read_dir,
  .delete = teep_sample_delete,
  .download = teep_sample_download,
  .save = teep_sample_save,
  .get_exts = sample_get_sample_extensions,
  .select_item = teep_sample_select,
  .get_download_path = common_system_get_download_path
};

static GByteArray *
teep_get_handshake_msg ()
{
  return teep_get_msg (TEEP_HANDSHAKE_MSG, sizeof (TEEP_HANDSHAKE_MSG));
}

static GByteArray *
teep_get_space_msg (guint8 id)
{
  GByteArray *msg = teep_get_msg (TEEP_SIZE_MSG, sizeof (TEEP_SIZE_MSG));
  msg->data[2] = id;
  return msg;
}

static gint
teep_get_storage_stats (struct backend *backend, guint8 type,
			struct backend_storage_stats *statfs,
			const gchar *path)
{
  gint err = 0;
  GError *error;
  JsonReader *reader;
  JsonParser *parser;
  GByteArray *rx_msg;

  if (type != 1)
    {
      return -EINVAL;
    }

  rx_msg = teep_get_sample_metadata (backend, 1000);	// 1000 ID is for stats
  if (!rx_msg)
    {
      return -EIO;
    }

  error = NULL;
  parser = json_parser_new ();
  json_parser_load_from_data (parser, (gchar *) rx_msg->data, -1, &error);
  if (error)
    {
      error_print ("Error wile loading JSON: %s", error->message);
      g_error_free (error);
      err = -EIO;
    }
  else
    {
      reader = json_reader_new (json_parser_get_root (parser));

      if (json_reader_read_member (reader, "max_capacity"))
	{
	  statfs->bsize = json_reader_get_int_value (reader);
	  json_reader_end_member (reader);
	}
      else
	{
	  err = -EIO;
	}

      if (json_reader_read_member (reader, "free_space_in_bytes"))
	{
	  statfs->bfree = json_reader_get_int_value (reader);
	  json_reader_end_member (reader);
	}
      else
	{
	  err = -EIO;
	}

      g_object_unref (reader);
    }

  g_object_unref (parser);
  free_msg (rx_msg);

  snprintf (statfs->name, LABEL_MAX, "%s", "samples");

  return err ? err : 0;
}

static gint
teep_handshake (struct backend *backend)
{
  GByteArray *tx_msg, *rx_msg;

  if (memcmp (backend->midi_info.company, TE_ID, sizeof (TE_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)))
    {
      return -ENODEV;
    }

  if (!memcmp (backend->midi_info.model, EP_133_ID, sizeof (EP_133_ID)))
    {
      guint16 *seq = g_malloc (sizeof (guint16));
      *seq = TEEP_SEQ_START;
      backend->data = seq;
      backend->destroy_data = backend_destroy_data;
      backend->get_storage_stats = teep_get_storage_stats;

      rx_msg = teep_tx_and_rx_sysex (backend, NULL, -1);
      if (rx_msg)
	{
	  gchar info[LABEL_MAX];
	  g_snprintf (info, LABEL_MAX, "%.*s", rx_msg->len, rx_msg->data);
	  free_msg (rx_msg);

	  debug_print (1, "Device information: %s", info);

	  gchar **tokens = g_strsplit_set (info, ";:", -1);
	  snprintf (backend->name, LABEL_MAX, "Teenage Engineering %s",
		    tokens[1]);
	  snprintf (backend->version, LABEL_MAX, "%s", tokens[9]);
	  g_strfreev (tokens);

	  gslist_fill (&backend->fs_ops, &FS_TEEP_SAMPLE_OPERATIONS, NULL);
	}
      else
	{
	  backend->destroy_data (backend);
	  return -ENODEV;
	}

      tx_msg = teep_get_handshake_msg ();	// Required but with unknown meaning.
      rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
      else
	{
	  backend->destroy_data (backend);
	  return -ENODEV;
	}

      tx_msg = teep_get_space_msg (0);	// Not required.
      rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
      else
	{
	  backend->destroy_data (backend);
	  return -ENODEV;
	}

      tx_msg = teep_get_space_msg (1);	// Not required.
      rx_msg = teep_tx_and_rx_sysex (backend, tx_msg, -1);
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
      else
	{
	  backend->destroy_data (backend);
	  return -ENODEV;
	}

      return 0;
    }
  else
    {
      return -ENODEV;
    }
}

const struct connector CONNECTOR_TEEP = {
  .name = "teep",
  .handshake = teep_handshake,
  .type = CONNECTOR_TYPE_MIDI,
  .regex = ".*EP-(133|1230|40).*"
};
