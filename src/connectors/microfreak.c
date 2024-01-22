/*
 *   microfreak.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include <zip.h>
#include "microfreak.h"
#include "common.h"

#define MICROFREAK_PRESET_NAME_LEN 14
#define MICROFREAK_MAX_PRESETS 512
#define MICROFREAK_REST_TIME_US 15000

#define MICROFREAK_GET_MSG_PAYLOAD_LEN(msg) (msg->data[7])
#define MICROFREAK_GET_MSG_PAYLOAD(msg) (&msg->data[9])
#define MICROFREAK_GET_ID_FROM_HEADER(header) ((header[0] << 7) | header[1])
#define MICROFREAK_GET_ID_IN_BANK_FROM_HEADER(header) (&header[8])	// Equals to header[1]
#define MICROFREAK_GET_INIT_FROM_HEADER(header) (&header[3])
#define MICROFREAK_GET_CATEGORY_FROM_HEADER(header) (&header[10])
#define MICROFREAK_GET_P1_FROM_HEADER(header) (&header[11])
#define MICROFREAK_GET_NAME_FROM_HEADER(header) ((gchar*)&header[12])

static const guint8 MICROFREAK_REQUEST_HEADER[] =
  { 0xf0, 0, 0x20, 0x6b, 7, 1 };

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 FAMILY_ID[] = { 0x6, 0x0 };
static const guint8 MODEL_ID[] = { 0x6, 0x1 };

enum microfreak_fs
{
  FS_MICROFREAK_PRESET = 1
};

struct microfreak_iter_data
{
  guint next;
  struct backend *backend;
};

static gchar *
microfreak_get_preset_download_path (struct backend *backend,
				     const struct fs_operations *ops,
				     const gchar *dst_dir,
				     const gchar *src_path,
				     GByteArray *preset)
{
  guint id;
  gint64 len;
  gchar *path;
  gchar *next;
  gchar name[MICROFREAK_PRESET_NAME_LEN + 1];

  if (!preset)
    {
      return NULL;
    }

  if (common_slot_get_id_name_from_path (src_path, &id, NULL))
    {
      return NULL;
    }

  len = g_ascii_strtoll ((gchar *) & preset->data[38], &next, 10);
  next++;
  memcpy (name, next, len);
  name[len] = 0;
  path = common_get_download_path_with_params (backend, ops, dst_dir, id, 3,
					       name);

  return path;
}

static void
microfreak_get_preset_name (gchar *preset_name, GByteArray *preset_rx)
{
  guint8 *payload = MICROFREAK_GET_MSG_PAYLOAD (preset_rx);
  gchar *name = MICROFREAK_GET_NAME_FROM_HEADER (payload);
  memcpy (preset_name, name, MICROFREAK_PRESET_NAME_LEN);
  preset_name[MICROFREAK_PRESET_NAME_LEN] = 0;
}

static GByteArray *
microfreak_get_msg (struct backend *backend, guint8 op, guint8 *data,
		    guint8 len)
{
  guint8 *seq = backend->data;
  GByteArray *tx_msg = g_byte_array_sized_new (256);
  g_byte_array_append (tx_msg, MICROFREAK_REQUEST_HEADER,
		       sizeof (MICROFREAK_REQUEST_HEADER));
  g_byte_array_append (tx_msg, seq, 1);
  if (!data)
    {
      len = 0;
    }
  g_byte_array_append (tx_msg, (guint8 *) & len, 1);
  g_byte_array_append (tx_msg, &op, 1);
  if (data)
    {
      g_byte_array_append (tx_msg, data, len);
    }
  g_byte_array_append (tx_msg, (guint8 *) "\xf7", 1);
  (*seq)++;
  if (*seq == 0x80)
    {
      *seq = 0;
    }
  return tx_msg;
}

static GByteArray *
microfreak_get_preset_dump_msg (struct backend *backend, guint32 id,
				guint8 part)
{
  guint8 payload[3];
  payload[0] = COMMON_GET_MIDI_BANK (id);
  payload[1] = COMMON_GET_MIDI_PRESET (id);
  payload[2] = part;
  return microfreak_get_msg (backend, 0x19, payload, 3);
}

static gint
microfreak_next_preset_dentry (struct item_iterator *iter)
{
  gint8 len;
  gchar preset_name[MICROFREAK_PRESET_NAME_LEN + 1];
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_iter_data *data = iter->data;

  if (data->next > MICROFREAK_MAX_PRESETS)
    {
      return -ENOENT;
    }

  tx_msg = microfreak_get_preset_dump_msg (data->backend, data->next - 1, 0);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = MICROFREAK_GET_MSG_PAYLOAD_LEN (rx_msg);
  if (len != MICROFREAK_PRESET_HEADER_MSG_LEN)
    {
      free_msg (rx_msg);
      return -EIO;
    }

  microfreak_get_preset_name (preset_name, rx_msg);
  snprintf (iter->item.name, LABEL_MAX, "%s", preset_name);
  free_msg (rx_msg);

  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  iter->item.slot_used = TRUE;
  (data->next)++;

  usleep (MICROFREAK_REST_TIME_US);

  return 0;
}

static gint
microfreak_preset_read_dir (struct backend *backend,
			    struct item_iterator *iter, const gchar *path,
			    const gchar **extensions)
{
  struct microfreak_iter_data *data;

  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct microfreak_iter_data));
  data->next = 1;
  data->backend = backend;
  iter->data = data;
  iter->next = microfreak_next_preset_dentry;
  iter->free = g_free;

  return 0;
}

gint
microfreak_deserialize_preset (struct microfreak_preset *mfp,
			       GByteArray *input)
{
  guint64 v;
  guint8 *p;
  gint err;

  err = memcmp (input->data, MICROFREAK_PRESET_HEADER,
		sizeof (MICROFREAK_PRESET_HEADER) - 1);
  if (err)
    {
      return -EINVAL;
    }

  memset (mfp, 0, sizeof (struct microfreak_preset));
  p = &input->data[sizeof (MICROFREAK_PRESET_HEADER) - 1];

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  p++;
  debug_print (2, "Deserializing preset '%.*s'...\n", (gint) v, p);
  memcpy (MICROFREAK_GET_NAME_FROM_HEADER (mfp->header), p, v);

  p += v;
  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  *MICROFREAK_GET_CATEGORY_FROM_HEADER (mfp->header) = v;

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  if (v != 0)
    {
      return -EINVAL;
    }

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  if (v != 0)
    {
      return -EINVAL;
    }

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  if (v != 18)
    {
      return -EINVAL;
    }

  p++;
  if (memcmp (p, (guint8 *) "000000000000000000 ", 19))
    {
      return -EINVAL;
    }
  p += 19;

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  *MICROFREAK_GET_INIT_FROM_HEADER (mfp->header) = v ? 0x08 : 0;

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  if (v != 0)
    {
      return -EINVAL;
    }

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  *MICROFREAK_GET_P1_FROM_HEADER (mfp->header) = v;

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  mfp->parts = v / MICROFREAK_PRESET_PART_LEN;

  for (gint i = 0; i < mfp->parts; i++)
    {
      for (gint j = 0; j < MICROFREAK_PRESET_PART_LEN; j++)
	{
	  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
	  mfp->part[i][j] = v;
	}
    }

  if (*p != 0x0a)
    {
      return -EINVAL;
    }

  return 0;
}

gint
microfreak_serialize_preset (GByteArray *output,
			     struct microfreak_preset *mfp)
{
  gchar aux[LABEL_MAX];
  guint8 *category = MICROFREAK_GET_CATEGORY_FROM_HEADER (mfp->header);
  gchar *name = MICROFREAK_GET_NAME_FROM_HEADER (mfp->header);
  gint namelen = strlen (name);

  g_byte_array_append (output, (guint8 *) MICROFREAK_PRESET_HEADER,
		       sizeof (MICROFREAK_PRESET_HEADER) - 1);

  debug_print (2, "Serializing preset '%.*s'...\n", (gint) namelen, name);
  snprintf (aux, LABEL_MAX, " %d ", namelen);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) name, namelen);

  snprintf (aux, LABEL_MAX, " %d", *category);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 18);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  g_byte_array_append (output, (guint8 *) " 000000000000000000", 19);

  snprintf (aux, LABEL_MAX, " %d",
	    (*MICROFREAK_GET_INIT_FROM_HEADER (mfp->header) & 0x08) ? 1 : 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d",
	    *MICROFREAK_GET_P1_FROM_HEADER (mfp->header));
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", mfp->parts * MICROFREAK_PRESET_PART_LEN);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  for (gint i = 0; i < mfp->parts; i++)
    {
      for (gint j = 0; j < MICROFREAK_PRESET_PART_LEN; j++)
	{
	  snprintf (aux, LABEL_MAX, " %d", mfp->part[i][j]);
	  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
	}
    }
  g_byte_array_append (output, (guint8 *) "\x0a", 1);

  return 0;
}

static gint
microfreak_download (struct backend *backend, const gchar *path,
		     GByteArray *output, struct job_control *control)
{
  guint id;
  gint8 len;
  gint err = 0;
  guint8 init, *payload;
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_preset mfp;

  err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_PRESETS)
    {
      return -EINVAL;
    }

  control->parts = 2 + MICROFREAK_PRESET_PARTS;	//Worst case
  control->part = 0;
  tx_msg = microfreak_get_preset_dump_msg (backend, id, 0);
  err = common_data_download_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  len = MICROFREAK_GET_MSG_PAYLOAD_LEN (rx_msg);
  if (rx_msg->len != 45 || len != MICROFREAK_PRESET_HEADER_MSG_LEN)
    {
      err = -EINVAL;
      free_msg (rx_msg);
      goto end;
    }
  payload = MICROFREAK_GET_MSG_PAYLOAD (rx_msg);
  init = (*MICROFREAK_GET_INIT_FROM_HEADER (payload)) & 0x08;
  memcpy (mfp.header, payload, len);
  free_msg (rx_msg);
  mfp.parts = init ? 0 : MICROFREAK_PRESET_PARTS;

  usleep (MICROFREAK_REST_TIME_US);

  if (init)
    {
      control->parts = 1;
      set_job_control_progress (control, 1.0);
      goto end;
    }

  control->part++;
  tx_msg = microfreak_get_preset_dump_msg (backend, id, 1);
  err = common_data_download_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  len = MICROFREAK_GET_MSG_PAYLOAD_LEN (rx_msg);
  if (rx_msg->len != 10 || len != 0)
    {
      err = -EINVAL;
      free_msg (rx_msg);
      goto end;
    }
  //Nothing to do with this response
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  for (gint i = 0; i < mfp.parts; i++)
    {
      guint8 payload = 0;
      control->part++;
      tx_msg = microfreak_get_msg (backend, 0x18, &payload, 1);
      err = common_data_download_part (backend, tx_msg, &rx_msg, control);
      if (err)
	{
	  goto end;
	}
      len = MICROFREAK_GET_MSG_PAYLOAD_LEN (rx_msg);
      if (rx_msg->len != 42 || len != MICROFREAK_PRESET_PART_LEN)
	{
	  err = -EINVAL;
	  free_msg (rx_msg);
	  goto end;
	}
      memcpy (mfp.part[i], MICROFREAK_GET_MSG_PAYLOAD (rx_msg), len);
      free_msg (rx_msg);

      usleep (MICROFREAK_REST_TIME_US);
    }

end:
  if (!err)
    {
      microfreak_serialize_preset (output, &mfp);
    }
  return err;
}

static gint
microfreak_upload (struct backend *backend, const gchar *path,
		   GByteArray *input, struct job_control *control)
{
  struct microfreak_preset mfp;
  GByteArray *tx_msg, *rx_msg;
  guint id;
  guint8 payload[3];
  gint err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_PRESETS)
    {
      return -EINVAL;
    }

  err = microfreak_deserialize_preset (&mfp, input);
  if (err)
    {
      return err;
    }

  control->parts = 3 + mfp.parts;
  control->part = 0;

  mfp.header[0] = COMMON_GET_MIDI_BANK (id);
  mfp.header[1] = COMMON_GET_MIDI_PRESET (id);
  *MICROFREAK_GET_ID_IN_BANK_FROM_HEADER (mfp.header) = mfp.header[1];
  tx_msg = microfreak_get_msg (backend, 0x52, mfp.header,
			       MICROFREAK_PRESET_HEADER_MSG_LEN);
  err = common_data_download_part (backend, tx_msg, &rx_msg, control);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  control->part++;
  payload[0] = COMMON_GET_MIDI_BANK (id);
  payload[1] = COMMON_GET_MIDI_PRESET (id);
  payload[2] = 1;
  tx_msg = microfreak_get_msg (backend, 0x52, payload, 3);
  err = common_data_download_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  control->part++;
  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  err = common_data_download_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  for (gint i = 0; i < mfp.parts; i++)
    {
      control->part++;
      guint8 op = (i < mfp.parts - 1) ? 0x16 : 0x17;
      tx_msg = microfreak_get_msg (backend, op, mfp.part[i],
				   MICROFREAK_PRESET_PART_LEN);
      err = common_data_download_part (backend, tx_msg, &rx_msg, control);
      free_msg (rx_msg);
      if (err)
	{
	  return err;
	}
      usleep (MICROFREAK_REST_TIME_US);
    }

  return 0;
}

static gchar *
microfreak_get_preset_id_as_slot (struct item *item, struct backend *backend)
{
  return common_get_id_as_slot_padded (item, backend, 3);
}

void
microfreak_midi_program_change (struct backend *backend, const gchar *dir,
				struct item *item)
{
  common_midi_program_change_int (backend, dir, item->id - 1);
}

static gint
microfreak_rename (struct backend *backend, const gchar *src,
		   const gchar *dst)
{
  guint id;
  gint err;
  gchar *name;
  guint8 *header_payload, len, payload[3];
  GByteArray *tx_msg, *rx_msg;

  debug_print (1, "Renaming preset...\n");
  err = common_slot_get_id_name_from_path (src, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_PRESETS)
    {
      return -EINVAL;
    }

  tx_msg = microfreak_get_preset_dump_msg (backend, id, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = MICROFREAK_GET_MSG_PAYLOAD_LEN (rx_msg);
  if (len != MICROFREAK_PRESET_HEADER_MSG_LEN)
    {
      free_msg (rx_msg);
      return -EIO;
    }

  usleep (MICROFREAK_REST_TIME_US);

  header_payload = MICROFREAK_GET_MSG_PAYLOAD (rx_msg);
  name = MICROFREAK_GET_NAME_FROM_HEADER (header_payload);
  len = strlen (dst);
  memcpy (name, dst, len);
  memset (name + len, 0, MICROFREAK_PRESET_NAME_LEN - len);
  tx_msg = microfreak_get_msg (backend, 0x52, header_payload,
			       MICROFREAK_PRESET_HEADER_MSG_LEN);
  free_msg (rx_msg);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  payload[0] = COMMON_GET_MIDI_BANK (id);
  payload[1] = COMMON_GET_MIDI_PRESET (id);
  payload[2] = 1;
  tx_msg = microfreak_get_msg (backend, 0x52, payload, 3);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  common_midi_program_change_int (backend, NULL, id);

  return 0;
}

static const struct fs_operations FS_MICROFREAK_PRESET_OPERATIONS = {
  .fs = FS_MICROFREAK_PRESET,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "preset",
  .gui_name = "Presets",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .max_name_len = MICROFREAK_PRESET_NAME_LEN,
  .readdir = microfreak_preset_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_preset_id_as_slot,
  .rename = microfreak_rename,
  .download = microfreak_download,
  .upload = microfreak_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_preset_download_path,
  .select_item = microfreak_midi_program_change
};

static const struct fs_operations *FS_MICROFREAK_OPERATIONS[] = {
  &FS_MICROFREAK_PRESET_OPERATIONS, NULL
};

gint
microfreak_handshake_int (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, ARTURIA_ID, sizeof (ARTURIA_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  if (backend->midi_info.version[0] != 5)
    {
      error_print ("MicroFreak firmware version must be 5");
      return -ENODEV;
    }

  return 0;
}

gint
microfreak_handshake (struct backend *backend)
{
  gint err;
  guint8 *seq;
  GByteArray *tx_msg, *rx_msg;

  seq = g_malloc (sizeof (guint8));
  *seq = 0;
  backend->data = seq;

  err = microfreak_handshake_int (backend);
  if (err)
    {
      //After booting, the MicroFreak needs this message to start responding.
      tx_msg = microfreak_get_preset_dump_msg (backend, 0, 0);
      rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  g_free (backend->data);
	  backend->data = NULL;
	  return -ENODEV;
	}
      free_msg (rx_msg);
      usleep (MICROFREAK_REST_TIME_US);
      backend_midi_handshake (backend);
      err = microfreak_handshake_int (backend);
      if (err)
	{
	  g_free (backend->data);
	  backend->data = NULL;
	  return err;
	}
    }

  backend->filesystems = FS_MICROFREAK_PRESET;
  backend->fs_ops = FS_MICROFREAK_OPERATIONS;
  backend->destroy_data = backend_destroy_data;

  snprintf (backend->name, LABEL_MAX, "Arturia MicroFreak");

  return 0;
}
