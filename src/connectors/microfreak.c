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
#define MICROFREAK_MAX_SAMPLES 128
#define MICROFREAK_REST_TIME_US 25000
#define MICROFREAK_SAMPLE_TIME_S 210
#define MICROFREAK_SAMPLE_SIZE_PER_S (32000 * 2)	// at 32 kHz 16 bits
#define MICROFREAK_SAMPLE_MEM_SIZE (MICROFREAK_SAMPLE_TIME_S * MICROFREAK_SAMPLE_SIZE_PER_S)
#define MICROFREAK_SAMPLE_BLK_SHRT 14
#define MICROFREAK_SAMPLE_BLK_SIZE (MICROFREAK_SAMPLE_BLK_SHRT * 2)
#define MICROFREAK_SAMPLE_MSG_SIZE (MICROFREAK_SAMPLE_BLK_SIZE * 8 / 7)

#define MICROFREAK_GET_MSG_PAYLOAD_LEN(msg) (msg->data[7])
#define MICROFREAK_GET_MSG_OP(msg) (msg->data[8])
#define MICROFREAK_GET_MSG_PAYLOAD(msg) (&msg->data[9])
#define MICROFREAK_GET_ID_FROM_HEADER(header) ((header[0] << 7) | header[1])
#define MICROFREAK_GET_ID_IN_BANK_FROM_HEADER(header) (&header[8])	// Equals to header[1]
#define MICROFREAK_GET_INIT_FROM_HEADER(header) (&header[3])
#define MICROFREAK_GET_CATEGORY_FROM_HEADER(header) (&header[10])
#define MICROFREAK_GET_P1_FROM_HEADER(header) (&header[11])
#define MICROFREAK_GET_NAME_FROM_HEADER(header) ((gchar*)&header[12])

#define MICROFREAK_CHECK_OP_LEN(msg,op,len) (MICROFREAK_GET_MSG_OP(msg) == op && MICROFREAK_GET_MSG_PAYLOAD_LEN(msg) == len ? 0 : -EIO)

#define MICROFREAK_ALPHABET " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"
#define MICROFREAK_DEFAULT_CHAR '.'

static const guint8 MICROFREAK_REQUEST_HEADER[] =
  { 0xf0, 0, 0x20, 0x6b, 7, 1 };

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 FAMILY_ID[] = { 0x6, 0x0 };
static const guint8 MODEL_ID[] = { 0x6, 0x1 };

enum microfreak_fs
{
  FS_MICROFREAK_PRESET = 1,
  FS_MICROFREAK_ZPRESET = 2,
  FS_MICROFREAK_SAMPLE = 4,
};

struct microfreak_iter_data
{
  guint next;
  struct backend *backend;
};

struct microfreak_sample_header *
microfreak_msg_to_sample_header (guint8 *payload)
{
  struct microfreak_sample_header *header =
    g_malloc (sizeof (struct microfreak_sample_header));
  guint8 *dst = (guint8 *) header;
  guint8 *src = payload;
  for (gint i = 0; i < 4; i++)
    {
      guint8 bits = *src;
      src++;
      for (gint j = 0; j < 7; j++, src++, dst++)
	{
	  *dst = *src | (bits & 0x1 ? 0x80 : 0);
	  bits >>= 1;
	}
    }
  header->size = GINT32_FROM_LE (header->size);
  return header;
}

static guint8 *
microfreak_sample_bytes_to_msg (guint8 *bytes)
{
  guint8 *msg = g_malloc (MICROFREAK_SAMPLE_MSG_SIZE);
  guint8 *dst = msg;
  guint8 *src = (guint8 *) bytes;
  for (gint i = 0; i < 4; i++)
    {
      guint8 *bits = dst;
      *bits = 0;
      dst++;
      for (gint j = 0; j < 7; j++, src++, dst++)
	{
	  *dst = *src & 0x7f;
	  *bits |= *src & 0x80;
	  *bits >>= 1;
	}
    }
  return msg;
}

guint8 *
microfreak_sample_header_to_msg (struct microfreak_sample_header *header)
{
  struct microfreak_sample_header iheader;
  memcpy (&iheader, header, sizeof (iheader));
  iheader.size = GINT32_TO_LE (iheader.size);
  return microfreak_sample_bytes_to_msg ((guint8 *) & iheader);
}

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
microfreak_get_msg (struct backend *backend, guint8 op, void *data,
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
      g_byte_array_append (tx_msg, (guint8 *) data, len);
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
microfreak_get_msg_from_sample_header (struct backend *backend, guint8 op,
				       struct microfreak_sample_header
				       *header)
{
  guint8 *payload = microfreak_sample_header_to_msg (header);
  GByteArray *msg =
    microfreak_get_msg (backend, op, payload, MICROFREAK_SAMPLE_MSG_SIZE);
  g_free (payload);
  return msg;
}

static GByteArray *
microfreak_get_preset_dump_msg (struct backend *backend, guint32 id,
				guint8 last)
{
  guint8 payload[3];
  payload[0] = COMMON_GET_MIDI_BANK (id);
  payload[1] = COMMON_GET_MIDI_PRESET (id);
  payload[2] = last;
  return microfreak_get_msg (backend, 0x19, payload, 3);
}

static gint
microfreak_next_preset_dentry (struct item_iterator *iter)
{
  gint err;
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
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x52,
				 MICROFREAK_PRESET_HEADER_MSG_LEN);
  if (err)
    {
      goto end;
    }

  microfreak_get_preset_name (preset_name, rx_msg);
  snprintf (iter->item.name, LABEL_MAX, "%s", preset_name);

  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  iter->item.slot_used = TRUE;
  (data->next)++;

end:
  free_msg (rx_msg);
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
microfreak_preset_download (struct backend *backend, const gchar *path,
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
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
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
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
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
      control->part++;
      tx_msg = microfreak_get_msg (backend, 0x18, "\x00", 1);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
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
  usleep (MICROFREAK_REST_TIME_US);	//Additional rest
  return err;
}

static gint
microfreak_preset_upload (struct backend *backend, const gchar *path,
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
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
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
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  control->part++;
  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
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
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      free_msg (rx_msg);
      if (err)
	{
	  return err;
	}
      usleep (MICROFREAK_REST_TIME_US);
    }

  usleep (MICROFREAK_REST_TIME_US);	//Additional rest
  return 0;
}

static gchar *
microfreak_get_object_id_as_slot (struct item *item, struct backend *backend)
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
microfreak_preset_rename (struct backend *backend, const gchar *src,
			  const gchar *dst)
{
  guint id;
  gint err;
  gchar *name, *sanitized;
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
  sanitized = common_get_sanitized_name (dst, MICROFREAK_ALPHABET,
					 MICROFREAK_DEFAULT_CHAR);
  len = strlen (sanitized);
  len = len > MICROFREAK_PRESET_NAME_LEN ? MICROFREAK_PRESET_NAME_LEN : len;
  memcpy (name, sanitized, len);
  g_free (sanitized);
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
  .get_slot = microfreak_get_object_id_as_slot,
  .rename = microfreak_preset_rename,
  .download = microfreak_preset_download,
  .upload = microfreak_preset_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_preset_download_path,
  .select_item = microfreak_midi_program_change
};

gint
microfreak_save_zpreset (const gchar *path, GByteArray *array,
			 struct job_control *control)
{
  gint err = 0, index;
  zip_t *archive;
  zip_error_t zerror;
  zip_source_t *source;

  zip_error_init (&zerror);

  archive = zip_open (path, ZIP_CREATE, &err);
  if (!archive)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while saving zip file: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  source = zip_source_buffer (archive, array->data, array->len, 0);
  if (!source)
    {
      error_print ("Error while creating source buffer: %s\n",
		   zip_strerror (archive));
      err = -EIO;
      goto end;
    }

  //Any name works as long as its a number, an underscore and additional characters without spaces.
  index = zip_file_add (archive, "0_preset", source, ZIP_FL_OVERWRITE);
  if (index < 0)
    {
      error_print ("Error while adding to file: %s\n",
		   zip_strerror (archive));
      err = -EIO;
      goto end;
    }

  if (zip_close (archive))
    {
      error_print ("Error while saving zip file: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      err = -EIO;
    }

end:
  if (err)
    {
      zip_discard (archive);
    }
  return err;
}

gint
microfreak_load_zpreset (const char *path, GByteArray *array,
			 struct job_control *control)
{
  gint err = 0;
  zip_t *archive;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_file_t *file = NULL;

  archive = zip_open (path, ZIP_RDONLY, &err);
  if (!archive)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while saving zip file: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  if (zip_get_num_entries (archive, 0) != 1)
    {
      err = -EIO;
      goto end;
    }

  file = zip_fopen_index (archive, 0, 0);
  if (!file)
    {
      err = -EIO;
      goto end;
    }

  if (zip_stat_index (archive, 0, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }

  g_byte_array_set_size (array, zstat.size);
  zip_fread (file, array->data, zstat.size);

end:
  if (file)
    {
      zip_fclose (file);
    }
  err = zip_close (archive) ? -EIO : 0;
  return err;
}

static const struct fs_operations FS_MICROFREAK_ZPRESET_OPERATIONS = {
  .fs = FS_MICROFREAK_ZPRESET,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "zpreset",
  .gui_name = "Presets",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "mfpz",
  .max_name_len = MICROFREAK_PRESET_NAME_LEN,
  .readdir = microfreak_preset_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_object_id_as_slot,
  .rename = microfreak_preset_rename,
  .download = microfreak_preset_download,
  .upload = microfreak_preset_upload,
  .load = microfreak_load_zpreset,
  .save = microfreak_save_zpreset,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_preset_download_path,
  .select_item = microfreak_midi_program_change
};

static GByteArray *
microfreak_get_sample_op_msg (struct backend *backend, guint8 op, guint8 id,
			      guint8 last)
{
  guint8 payload[3];
  payload[0] = COMMON_GET_MIDI_PRESET (id);
  payload[1] = 0;
  payload[2] = last;
  return microfreak_get_msg (backend, op, payload, 3);
}

static gint
microfreak_next_sample_dentry (struct item_iterator *iter)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_sample_header *header;
  struct microfreak_iter_data *data = iter->data;

  if (data->next > MICROFREAK_MAX_SAMPLES)
    {
      return -ENOENT;
    }

  tx_msg = microfreak_get_sample_op_msg (data->backend, 0x5b, data->next - 1,
					 0);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x15, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  tx_msg = microfreak_get_msg (data->backend, 0x18, "\x01", 1);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x16, MICROFREAK_SAMPLE_MSG_SIZE);
  if (err)
    {
      goto end;
    }

  header =
    microfreak_msg_to_sample_header (MICROFREAK_GET_MSG_PAYLOAD (rx_msg));
  snprintf (iter->item.name, LABEL_MAX, "%s", header->name);
  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = header->size;
  iter->item.slot_used = TRUE;
  (data->next)++;
  g_free (header);

end:
  free_msg (rx_msg);
  usleep (MICROFREAK_REST_TIME_US);
  return err;
}

static gint
microfreak_sample_read_dir (struct backend *backend,
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
  iter->next = microfreak_next_sample_dentry;
  iter->free = g_free;

  return 0;
}

static gint
microfreak_reset_sample (struct backend *backend, guint id,
			 struct microfreak_sample_header *header)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = microfreak_get_sample_op_msg (backend, 0x5a, id, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg_from_sample_header (backend, 0x17, header);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);

end:
  usleep (MICROFREAK_REST_TIME_US);
  return err;
}

static gint
microfreak_clear_sample (struct backend *backend, const gchar *path)
{
  gint err;
  guint id;
  struct microfreak_sample_header header;

  err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_SAMPLES)
    {
      return -EINVAL;
    }

  memset (&header, 0, sizeof (header));
  header.id = id;
  return microfreak_reset_sample (backend, id, &header);
}

static gint
microfreak_upload_sample (struct backend *backend, const gchar *path,
			  GByteArray *input, struct job_control *control)
{
  gint err;
  guint id, parts;
  gchar *name, *sanitized;
  struct microfreak_sample_header header;
  GByteArray *tx_msg, *rx_msg;

  err = common_slot_get_id_name_from_path (path, &id, &name);
  if (err)
    {
      return err;
    }

  sanitized = common_get_sanitized_name (name, MICROFREAK_ALPHABET,
					 MICROFREAK_DEFAULT_CHAR);

  id--;
  if (id >= MICROFREAK_MAX_SAMPLES)
    {
      err = -EINVAL;
      goto end;
    }

  parts = input->len / MICROFREAK_SAMPLE_BLK_SIZE;
  parts += (input->len % MICROFREAK_SAMPLE_BLK_SIZE) ? 1 : 0;
  control->parts = 7 + parts;
  control->part = 0;

  tx_msg = microfreak_get_sample_op_msg (backend, 0x5d, id, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  memset (&header, 0, sizeof (header));
  header.size = input->len;
  snprintf (header.name, MICROFREAK_SAMPLE_NAME_LEN, "%s", sanitized);
  header.id = id;

  tx_msg = microfreak_get_msg_from_sample_header (backend, 0x17, &header);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x16, 1);
  if (!err)
    {
      err = *MICROFREAK_GET_MSG_PAYLOAD (rx_msg) == 1 ? 0 : -EIO;
    }
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = g_byte_array_new ();	//This is an empty message
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  err = microfreak_reset_sample (backend, id, &header);
  if (err)
    {
      goto end;
    }

  control->part++;
  set_job_control_progress (control, 1.0);
  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_sample_op_msg (backend, 0x58, id, 1);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  control->part++;
  usleep (MICROFREAK_REST_TIME_US);

  guint32 total = 0;
  gint16 *src = (gint16 *) input->data;
  for (gint i = 0; i < parts; i++)
    {
      guint8 op = (i < parts - 1) ? 0x16 : 0x17;
      gint16 blk[MICROFREAK_SAMPLE_BLK_SHRT];
      gint16 *dst = blk;

      gint j;
      for (j = 0; j < MICROFREAK_SAMPLE_BLK_SHRT && total < input->len; j++)
	{
	  *dst = GINT16_TO_LE (*src);
	  dst++;
	  src++;
	  total += 2;
	}
      for (; j < MICROFREAK_SAMPLE_BLK_SHRT; j++)
	{
	  *dst = 0;
	  dst++;
	}

      control->part++;
      guint8 *msg = microfreak_sample_bytes_to_msg ((guint8 *) blk);
      tx_msg = microfreak_get_msg (backend, op, msg,
				   MICROFREAK_SAMPLE_MSG_SIZE);
      g_free (msg);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      if (err)
	{
	  goto end;
	}
      err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
      free_msg (rx_msg);
      if (err)
	{
	  goto end;
	}

      usleep (MICROFREAK_REST_TIME_US);
    }

  //From here till the end, the messages seem to properly commit the transferred data.

  tx_msg = microfreak_get_sample_op_msg (backend, 0x5b, id, 1);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x15, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  while (1)
    {
      tx_msg = microfreak_get_msg (backend, 0x18, "\x00", 1);
      rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  return -EIO;
	}

      gint end = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x16,
					  MICROFREAK_SAMPLE_MSG_SIZE);
      free_msg (rx_msg);
      if (end)
	{
	  break;
	}

      usleep (MICROFREAK_REST_TIME_US);
    }

end:
  g_free (name);
  g_free (sanitized);
  usleep (MICROFREAK_REST_TIME_US);
  return err;
}

static guint64
microfreak_get_bfree_from_msg (GByteArray *rx_msg)
{
  guint8 *payload, lsb, msb;
  gfloat tused, tfree, used;

  payload = MICROFREAK_GET_MSG_PAYLOAD (rx_msg);
  lsb = payload[6] | (payload[2] & 0x08 ? 0x80 : 0);
  msb = payload[7] | (payload[2] & 0x04 ? 0x80 : 0);
  tused = (((msb << 8) | lsb) << 2) / 1000.0;

  tfree = MICROFREAK_SAMPLE_TIME_S - tused;
  used = tfree / (gdouble) MICROFREAK_SAMPLE_TIME_S;
  return MICROFREAK_SAMPLE_MEM_SIZE * used;
}

static gint
microfreak_get_storage_stats (struct backend *backend, gint type,
			      struct backend_storage_stats *statfs,
			      const gchar *path)
{
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = microfreak_get_msg (backend, 0x47, "\x0a", 1);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (MICROFREAK_CHECK_OP_LEN (rx_msg, 0x48, 9))
    {
      err = -EIO;
      goto err;
    }

  snprintf (statfs->name, LABEL_MAX, "%s", "sample");
  statfs->bfree = microfreak_get_bfree_from_msg (rx_msg);
  statfs->bsize = MICROFREAK_SAMPLE_MEM_SIZE;

err:
  free_msg (rx_msg);
  usleep (MICROFREAK_REST_TIME_US);
  return err;
}

static gint
microfreak_sample_load (const gchar *path, GByteArray *sample,
			struct job_control *control)
{
  return common_sample_load (path, sample, control, 32000, 1,
			     SF_FORMAT_PCM_16);
}

static const struct fs_operations FS_MICROFREAK_SAMPLE_OPERATIONS = {
  .fs = FS_MICROFREAK_SAMPLE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_SHOW_SIZE_COLUMN |
    FS_OPTION_ALLOW_SEARCH,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = microfreak_sample_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_object_id_as_slot,
  .delete = microfreak_clear_sample,
  .clear = microfreak_clear_sample,
  .upload = microfreak_upload_sample,
  .load = microfreak_sample_load,
  .get_upload_path = common_slot_get_upload_path,
};

static const struct fs_operations *FS_MICROFREAK_OPERATIONS[] = {
  &FS_MICROFREAK_PRESET_OPERATIONS, &FS_MICROFREAK_ZPRESET_OPERATIONS,
  &FS_MICROFREAK_SAMPLE_OPERATIONS, NULL
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

  backend->filesystems = FS_MICROFREAK_ZPRESET | FS_MICROFREAK_SAMPLE;
  backend->fs_ops = FS_MICROFREAK_OPERATIONS;
  backend->destroy_data = backend_destroy_data;
  backend->get_storage_stats = microfreak_get_storage_stats;
  backend->storage = 1;

  snprintf (backend->name, LABEL_MAX, "Arturia MicroFreak");

  return 0;
}
