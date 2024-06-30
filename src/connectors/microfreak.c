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
#define MICROFREAK_REST_TIME_US 5000
#define MICROFREAK_REST_TIME_LONG_US 20000
#define MICROFREAK_SAMPLERATE 32000
#define MICROFREAK_SAMPLE_TIME_S 210
#define MICROFREAK_SAMPLE_SIZE_PER_S (MICROFREAK_SAMPLERATE * MICROFREAK_SAMPLE_SIZE)	// at 32 kHz 16 bits
#define MICROFREAK_SAMPLE_MEM_SIZE (MICROFREAK_SAMPLE_TIME_S * MICROFREAK_SAMPLE_SIZE_PER_S)
#define MICROFREAK_SAMPLE_BATCH_SIZE 4096	// 147 packets (146 * 28 + 8)
#define MICROFREAK_SAMPLE_BATCH_LEN (MICROFREAK_SAMPLE_BATCH_SIZE / MICROFREAK_SAMPLE_SIZE)
#define MICROFREAK_SAMPLE_BATCH_PACKETS 147
#define MICROFREAK_MAX_WAVETABLES 16
#define MICROFREAK_WAVETABLE_PARTS 4
#define MICROFREAK_WAVETABLE_FRAMES_PER_BATCH (MICROFREAK_SAMPLE_BATCH_SIZE / (MICROFREAK_WAVETABLE_CYCLES * MICROFREAK_SAMPLE_SIZE))

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

#define MICROFREAK_PPRESET_EXT "mfp"
#define MICROFREAK_ZPRESET_EXT "mfpz"

#define MICROFREAK_PWAVETABLE_EXT "mfw"
#define MICROFREAK_ZWAVETABLE_EXT "mfwz"

#define MICROFREAK_WAVETABLE_EMPTY 0x08

static const guint8 MICROFREAK_REQUEST_HEADER[] =
  { 0xf0, 0, 0x20, 0x6b, 7, 1 };

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 FAMILY_ID[] = { 0x6, 0x0 };
static const guint8 MODEL_ID[] = { 0x6, 0x1 };

enum microfreak_fs
{
  FS_MICROFREAK_PPRESET,	//Plain presets
  FS_MICROFREAK_ZPRESET,	//Zipped presets
  FS_MICROFREAK_PRESET,		//Loads ppreset and zpreset and stores zpreset
  FS_MICROFREAK_SAMPLE,
  FS_MICROFREAK_PWAVETABLE,	//Plain wavetables
  FS_MICROFREAK_ZWAVETABLE,	//Zipped wavetables
  FS_MICROFREAK_WAVETABLE	//Loads pwavetable, zwavetable and audio files and stores wav files
};

struct microfreak_iter_data
{
  guint next;
  struct backend *backend;
};

void
microfreak_midi_msg_to_8bit_msg (guint8 *msg_midi, guint8 *msg_8bit)
{
  guint8 *dst = msg_8bit;
  guint8 *src = msg_midi;
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
}

void
microfreak_8bit_msg_to_midi_msg (guint8 *msg_8bit, guint8 *msg_midi)
{
  guint8 *dst = msg_midi;
  guint8 *src = msg_8bit;
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
}

static gchar *
microfreak_get_download_preset_path (struct backend *backend,
				     const struct fs_operations *ops,
				     const gchar *dst_dir,
				     const gchar *src_path,
				     struct idata *preset)
{
  guint id;
  gint64 len;
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

  len = g_ascii_strtoll ((gchar *) & preset->content->data[38], &next, 10);
  next++;
  memcpy (name, next, len);
  name[len] = 0;

  return common_get_download_path_with_params (backend, ops, dst_dir, id, 3,
					       name);
}

static void
microfreak_preset_get_name (gchar *preset_name, GByteArray *preset_rx)
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
microfreak_get_msg_from_8bit_msg (struct backend *backend, guint8 op,
				  guint8 *msg_8bit)
{
  guint8 midi_msg[MICROFREAK_WAVE_MSG_SIZE];
  microfreak_8bit_msg_to_midi_msg (msg_8bit, midi_msg);
  GByteArray *msg = microfreak_get_msg (backend, op, midi_msg,
					MICROFREAK_WAVE_MSG_SIZE);
  return msg;
}

static GByteArray *
microfreak_get_preset_op_msg (struct backend *backend, guint8 op, guint id,
			      guint8 data)
{
  guint8 payload[3];
  payload[0] = COMMON_GET_MIDI_BANK (id);
  payload[1] = COMMON_GET_MIDI_PRESET (id);
  payload[2] = data;
  return microfreak_get_msg (backend, op, payload, 3);
}

static const gchar *
microfreak_get_category_name (GByteArray *rx_msg)
{
  gint8 category_id =
    *MICROFREAK_GET_CATEGORY_FROM_HEADER (MICROFREAK_GET_MSG_PAYLOAD
					  (rx_msg));
  switch (category_id)
    {
    case 0:
      return "Bass";
    case 1:
      return "Brass";
    case 2:
      return "Keys";
    case 3:
      return "Lead";
    case 4:
      return "Organ";
    case 5:
      return "Pad";
    case 6:
      return "Percussion";
    case 7:
      return "Sequence";
    case 8:
      return "SFX";
    case 9:
      return "Strings";
    case 10:
      return "Template";
    case 11:
      return "Vocoder";
    default:
      return "";
    }
}

static gint
microfreak_common_read_dir (struct backend *backend,
			    struct item_iterator *iter, const gchar *dir,
			    iterator_next next)
{
  struct microfreak_iter_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct microfreak_iter_data));
  data->next = 1;
  data->backend = backend;

  item_iterator_init (iter, dir, data, next, g_free);

  return 0;
}

static gint
microfreak_next_preset_dentry (struct item_iterator *iter)
{
  gint err;
  const gchar *category;
  gchar preset_name[MICROFREAK_PRESET_NAME_LEN + 1];
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_iter_data *data = iter->data;
  guint id = data->next - 1;

  if (data->next > MICROFREAK_MAX_PRESETS)
    {
      return -ENOENT;
    }

  tx_msg = microfreak_get_preset_op_msg (data->backend, 0x19, id, 0);
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

  microfreak_preset_get_name (preset_name, rx_msg);
  snprintf (iter->item.name, LABEL_MAX, "%s", preset_name);

  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  category = microfreak_get_category_name (rx_msg);
  snprintf (iter->item.object_info, LABEL_MAX, "%s", category);
  (data->next)++;

end:
  free_msg (rx_msg);
  usleep (MICROFREAK_REST_TIME_LONG_US);
  return 0;
}

static gint
microfreak_preset_read_dir (struct backend *backend,
			    struct item_iterator *iter, const gchar *path,
			    GSList *extensions)
{
  return microfreak_common_read_dir (backend, iter, path,
				     microfreak_next_preset_dentry);
}

static gint
microfreak_deserialize_object (GByteArray *input, gchar *header,
			       guint headerlen, gchar *name, guint8 *p0,
			       guint8 *p3, guint8 *p5, guint8 *data,
			       guint *datalen)
{
  guint64 v;
  guint8 *p;
  gint err;

  err = memcmp (input->data, (guint8 *) header, headerlen);
  if (err)
    {
      return -EINVAL;
    }

  p = &input->data[headerlen];

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  p++;
  memcpy (name, p, v);
  name[v] = 0;
  debug_print (2, "Deserializing object '%s'...\n", name);

  p += v;
  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  *p0 = v;

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
  *p3 = v;

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  if (v != 0)
    {
      return -EINVAL;
    }

  v = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
  *p5 = v;

  *datalen = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);

  for (guint i = 0; i < *datalen; i++, data++)
    {
      *data = g_ascii_strtoll ((gchar *) p, (gchar **) & p, 10);
    }

  if (*p != 0x0a)
    {
      return -EINVAL;
    }

  return 0;
}

gint
microfreak_deserialize_preset (struct microfreak_preset *mfp,
			       GByteArray *input)
{
  gchar *name = MICROFREAK_GET_NAME_FROM_HEADER (mfp->header);
  guint8 *category = MICROFREAK_GET_CATEGORY_FROM_HEADER (mfp->header);
  guint8 *init = MICROFREAK_GET_INIT_FROM_HEADER (mfp->header);
  guint8 *p1 = MICROFREAK_GET_P1_FROM_HEADER (mfp->header);
  guint datalen = 0;
  gint err;

  memset (mfp, 0, sizeof (struct microfreak_preset));
  err = microfreak_deserialize_object (input, MICROFREAK_PRESET_HEADER,
				       sizeof (MICROFREAK_PRESET_HEADER) - 1,
				       name, category, init, p1, mfp->data,
				       &datalen);
  *init = *init ? 0x08 : 0;
  mfp->parts = datalen / MICROFREAK_PRESET_PART_LEN;
  return err;
}

static gint
microfreak_serialize_object (GByteArray *output, gchar *header,
			     guint headerlen, const gchar *name,
			     guint8 p0, guint8 p3, guint8 p5,
			     guint8 *data, guint datalen)
{
  gchar aux[LABEL_MAX];
  guint namelen = strlen (name);
  gint8 *v;

  g_byte_array_append (output, (guint8 *) header, headerlen);

  debug_print (2, "Serializing object '%s'...\n", name);
  snprintf (aux, LABEL_MAX, " %d ", namelen);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) name, namelen);

  snprintf (aux, LABEL_MAX, " %d", p0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  snprintf (aux, LABEL_MAX, " %d", 18);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) " 000000000000000000", 19);

  snprintf (aux, LABEL_MAX, " %d", p3);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", p5);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  snprintf (aux, LABEL_MAX, " %d", datalen);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  v = (gint8 *) data;
  for (guint i = 0; i < datalen; i++, v++)
    {
      snprintf (aux, LABEL_MAX, " %d", *v);
      g_byte_array_append (output, (guint8 *) aux, strlen (aux));
    }

  g_byte_array_append (output, (guint8 *) "\x0a", 1);

  return 0;
}

gint
microfreak_serialize_preset (GByteArray *output,
			     struct microfreak_preset *mfp)
{
  guint8 category = *MICROFREAK_GET_CATEGORY_FROM_HEADER (mfp->header);
  const gchar *name = MICROFREAK_GET_NAME_FROM_HEADER (mfp->header);
  guint8 init = *MICROFREAK_GET_INIT_FROM_HEADER (mfp->header);
  guint8 p1 = *MICROFREAK_GET_P1_FROM_HEADER (mfp->header);

  init = init & 0x08 ? 1 : 0;
  return microfreak_serialize_object (output, MICROFREAK_PRESET_HEADER,
				      sizeof (MICROFREAK_PRESET_HEADER) - 1,
				      name, category, init, p1, mfp->data,
				      mfp->parts *
				      MICROFREAK_PRESET_PART_LEN);
}

static gint
microfreak_preset_download (struct backend *backend, const gchar *path,
			    struct idata *preset, struct job_control *control)
{
  guint id;
  gint err;
  guint8 init, *payload;
  GByteArray *tx_msg, *rx_msg, *output;
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

  output = g_byte_array_new ();
  control->parts = 2 + MICROFREAK_PRESET_PARTS;	//Worst case
  control->part = 0;
  set_job_control_progress (control, 0.0);

  tx_msg = microfreak_get_preset_op_msg (backend, 0x19, id, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x52,
				 MICROFREAK_PRESET_HEADER_MSG_LEN);
  if (err)
    {
      err = -EINVAL;
      free_msg (rx_msg);
      goto end;
    }
  payload = MICROFREAK_GET_MSG_PAYLOAD (rx_msg);
  init = (*MICROFREAK_GET_INIT_FROM_HEADER (payload)) & 0x08;
  memcpy (mfp.header, payload, MICROFREAK_PRESET_HEADER_MSG_LEN);
  free_msg (rx_msg);
  mfp.parts = init ? 0 : MICROFREAK_PRESET_PARTS;

  usleep (MICROFREAK_REST_TIME_US);

  if (init)
    {
      control->parts = 1;
      control->part = 0;
      set_job_control_progress (control, 1.0);
      goto end;
    }

  tx_msg = microfreak_get_preset_op_msg (backend, 0x19, id, 1);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x15, 0);
  free_msg (rx_msg);
  if (err)
    {
      err = -EINVAL;
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  for (gint i = 0; i < mfp.parts; i++)
    {
      gboolean active;
      guint8 op = i == mfp.parts - 1 ? 0x17 : 0x16;

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      if (!active)
	{
	  err = -ECANCELED;
	  goto end;
	}

      tx_msg = microfreak_get_msg (backend, 0x18, "\x00", 1);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      if (err)
	{
	  goto end;
	}
      err = MICROFREAK_CHECK_OP_LEN (rx_msg, op, MICROFREAK_PRESET_PART_LEN);
      if (err)
	{
	  err = -EINVAL;
	  free_msg (rx_msg);
	  goto end;
	}
      memcpy (&mfp.data[i * MICROFREAK_PRESET_PART_LEN],
	      MICROFREAK_GET_MSG_PAYLOAD (rx_msg),
	      MICROFREAK_PRESET_PART_LEN);
      free_msg (rx_msg);

      usleep (MICROFREAK_REST_TIME_US);
    }

end:
  if (err)
    {
      g_byte_array_free (output, TRUE);
    }
  else
    {
      microfreak_serialize_preset (output, &mfp);
      idata_init (preset, output, NULL, NULL);
    }
  usleep (MICROFREAK_REST_TIME_LONG_US);	//Additional rest
  return err;
}

static gint
microfreak_preset_upload (struct backend *backend, const gchar *path,
			  struct idata *preset, struct job_control *control)
{
  struct microfreak_preset mfp;
  GByteArray *tx_msg, *rx_msg;
  guint id;
  gint err;
  GByteArray *input = preset->content;

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

  err = microfreak_deserialize_preset (&mfp, input);
  if (err)
    {
      return err;
    }

  control->parts = 3 + mfp.parts;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  mfp.header[0] = COMMON_GET_MIDI_BANK (id);
  mfp.header[1] = COMMON_GET_MIDI_PRESET (id);
  *MICROFREAK_GET_ID_IN_BANK_FROM_HEADER (mfp.header) = mfp.header[1];
  tx_msg = microfreak_get_msg (backend, 0x52, mfp.header,
			       MICROFREAK_PRESET_HEADER_MSG_LEN);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_preset_op_msg (backend, 0x52, id, 1);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg (backend, 0x15, NULL, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  for (gint i = 0; i < mfp.parts; i++)
    {
      gboolean active;

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      if (!active)
	{
	  err = -ECANCELED;
	  break;
	}

      guint8 op = (i < mfp.parts - 1) ? 0x16 : 0x17;
      tx_msg = microfreak_get_msg (backend, op,
				   &mfp.data[i * MICROFREAK_PRESET_PART_LEN],
				   MICROFREAK_PRESET_PART_LEN);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      free_msg (rx_msg);
      if (err)
	{
	  return err;
	}

      usleep (MICROFREAK_REST_TIME_US);
    }

  usleep (MICROFREAK_REST_TIME_LONG_US);	//Additional rest
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
  guint8 *header_payload, len;
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

  tx_msg = microfreak_get_preset_op_msg (backend, 0x19, id, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x52,
				 MICROFREAK_PRESET_HEADER_MSG_LEN);
  if (err)
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

  tx_msg = microfreak_get_preset_op_msg (backend, 0x52, id, 1);
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

static const struct fs_operations FS_MICROFREAK_PPRESET_OPERATIONS = {
  .id = FS_MICROFREAK_PPRESET,
  .options = FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE,
  .name = "ppreset",
  .ext = MICROFREAK_PPRESET_EXT,
  .max_name_len = MICROFREAK_PRESET_NAME_LEN,
  .readdir = microfreak_preset_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_object_id_as_slot,
  .rename = microfreak_preset_rename,
  .download = microfreak_preset_download,
  .upload = microfreak_preset_upload,
  .load = file_load,
  .save = file_save,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_preset_path
};

static gint
microfreak_zobject_save (const gchar *path, struct idata *zobject,
			 struct job_control *control, const gchar *name)
{
  gint err = 0, index;
  zip_t *archive;
  zip_error_t zerror;
  zip_source_t *source;
  GByteArray *array = zobject->content;

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
  index = zip_file_add (archive, name, source, ZIP_FL_OVERWRITE);
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

static gint
microfreak_zpreset_save (const gchar *path, struct idata *zpreset,
			 struct job_control *control)
{
  return microfreak_zobject_save (path, zpreset, control, "0_preset");
}

static gint
microfreak_zobject_load (const char *path, struct idata *zobject,
			 struct job_control *control)
{
  gint err = 0;
  zip_t *archive;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_file_t *zip_file = NULL;
  GByteArray *array;

  archive = zip_open (path, ZIP_RDONLY, &err);
  if (!archive)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while opening zip file: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  array = g_byte_array_new ();
  if (zip_get_num_entries (archive, 0) != 1)
    {
      err = -EIO;
      goto end;
    }

  zip_file = zip_fopen_index (archive, 0, 0);
  if (!zip_file)
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
  zip_fread (zip_file, array->data, zstat.size);

end:
  if (zip_file)
    {
      zip_fclose (zip_file);
    }

  err = zip_close (archive) ? -EIO : 0;
  if (err)
    {
      g_byte_array_free (array, TRUE);
    }
  else
    {
      idata_init (zobject, array, NULL, NULL);
    }

  return err;
}

static const struct fs_operations FS_MICROFREAK_ZPRESET_OPERATIONS = {
  .id = FS_MICROFREAK_ZPRESET,
  .options = FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE,
  .name = "zpreset",
  .ext = MICROFREAK_ZPRESET_EXT,
  .max_name_len = MICROFREAK_PRESET_NAME_LEN,
  .readdir = microfreak_preset_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_object_id_as_slot,
  .rename = microfreak_preset_rename,
  .download = microfreak_preset_download,
  .upload = microfreak_preset_upload,
  .load = microfreak_zobject_load,
  .save = microfreak_zpreset_save,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_preset_path
};

static gint
microfreak_preset_load (const char *path, struct idata *preset,
			struct job_control *control)
{
  const gchar *ext = filename_get_ext (path);
  if (strcmp (ext, MICROFREAK_ZPRESET_EXT) == 0)
    {
      return microfreak_zobject_load (path, preset, control);
    }
  else
    {
      return file_load (path, preset, control);
    }
}

static GSList *
microfreak_preset_get_exts (struct backend *backend,
			    const struct fs_operations *ops)
{
  GSList *exts = g_slist_append (NULL, strdup (MICROFREAK_PPRESET_EXT));
  return g_slist_append (exts, strdup (MICROFREAK_ZPRESET_EXT));
}

static const struct fs_operations FS_MICROFREAK_PRESET_OPERATIONS = {
  .id = FS_MICROFREAK_PRESET,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_SHOW_INFO_COLUMN |
    FS_OPTION_ALLOW_SEARCH,
  .name = "preset",
  .gui_name = "Presets",
  .gui_icon = BE_FILE_ICON_SND,
  .ext = MICROFREAK_ZPRESET_EXT,
  .max_name_len = MICROFREAK_PRESET_NAME_LEN,
  .readdir = microfreak_preset_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_object_id_as_slot,
  .rename = microfreak_preset_rename,
  .download = microfreak_preset_download,
  .upload = microfreak_preset_upload,
  .load = microfreak_preset_load,
  .save = microfreak_zpreset_save,
  .get_exts = microfreak_preset_get_exts,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_preset_path,
  .select_item = microfreak_midi_program_change
};

static GByteArray *
microfreak_get_wave_op_msg (struct backend *backend, guint8 op, guint8 id,
			    guint8 part, guint8 data)
{
  guint8 payload[3];
  payload[0] = COMMON_GET_MIDI_PRESET (id);
  payload[1] = part;
  payload[2] = data;
  return microfreak_get_msg (backend, op, payload, 3);
}

static gint
microfreak_next_sample_dentry (struct item_iterator *iter)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_sample_header header;
  struct microfreak_iter_data *data = iter->data;

  if (data->next > MICROFREAK_MAX_SAMPLES)
    {
      return -ENOENT;
    }

  tx_msg = microfreak_get_wave_op_msg (data->backend, 0x5b, data->next - 1,
				       0, 0);
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

  tx_msg = microfreak_get_msg (data->backend, 0x18, "\x00", 1);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x16, MICROFREAK_WAVE_MSG_SIZE);
  if (err)
    {
      goto end;
    }

  microfreak_midi_msg_to_8bit_msg (MICROFREAK_GET_MSG_PAYLOAD (rx_msg),
				   (guint8 *) & header);
  snprintf (iter->item.name, LABEL_MAX, "%s", header.name);
  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = iter->item.size != 0 ? GINT32_FROM_LE (header.size) : 0;
  (data->next)++;

end:
  free_msg (rx_msg);
  usleep (MICROFREAK_REST_TIME_LONG_US);
  return err;
}

static gint
microfreak_sample_read_dir (struct backend *backend,
			    struct item_iterator *iter, const gchar *path,
			    GSList *extensions)
{
  return microfreak_common_read_dir (backend, iter, path,
				     microfreak_next_sample_dentry);
}

static gint
microfreak_sample_reset (struct backend *backend, guint id,
			 struct microfreak_sample_header *header)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = microfreak_get_wave_op_msg (backend, 0x5a, id, 0, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
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
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg_from_8bit_msg (backend, 0x17,
					     (guint8 *) header);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);

  usleep (MICROFREAK_REST_TIME_US);

  return err;
}

static gint
microfreak_sample_clear (struct backend *backend, const gchar *path)
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
  return microfreak_sample_reset (backend, id, &header);
}

static gint
microfreak_sample_upload (struct backend *backend, const gchar *path,
			  struct idata *sample, struct job_control *control)
{
  gint err;
  guint id, batches;
  gchar *name, *sanitized;
  struct microfreak_sample_header header;
  GByteArray *tx_msg, *rx_msg;
  GByteArray *input = sample->content;

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

  batches = input->len / MICROFREAK_SAMPLE_BATCH_SIZE;
  batches += (input->len % MICROFREAK_SAMPLE_BATCH_SIZE) ? 1 : 0;
  control->parts = 5 + batches * (2 + MICROFREAK_SAMPLE_BATCH_PACKETS);
  control->part = 0;
  set_job_control_progress (control, 0.0);

  tx_msg = microfreak_get_wave_op_msg (backend, 0x5d, id, 0, 0);
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

  usleep (MICROFREAK_REST_TIME_US);

  memset (&header, 0, sizeof (header));
  header.size = GINT32_TO_LE (input->len);
  snprintf (header.name, MICROFREAK_SAMPLE_NAME_LEN, "%s", sanitized);
  header.id = id;

  tx_msg = microfreak_get_msg_from_8bit_msg (backend, 0x17,
					     (guint8 *) & header);
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

  usleep (MICROFREAK_REST_TIME_US);

  err = microfreak_sample_reset (backend, id, &header);
  if (err)
    {
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  guint32 total = 0;
  gint16 *src = (gint16 *) input->data;
  for (gint b = 0; b < batches; b++)
    {
      //Starting packets

      tx_msg = microfreak_get_wave_op_msg (backend, 0x58, id, 0, 1);
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

      usleep (MICROFREAK_REST_TIME_US);

      //Data packets

      for (gint p = 1; p <= MICROFREAK_SAMPLE_BATCH_PACKETS; p++)
	{
	  guint8 op, midi_msg[MICROFREAK_WAVE_MSG_SIZE];
	  guint len;
	  gint16 blk[MICROFREAK_WAVE_BLK_SHRT];
	  gint16 *dst = blk;
	  gboolean active;

	  g_mutex_lock (&control->mutex);
	  active = control->active;
	  g_mutex_unlock (&control->mutex);

	  if (!active)
	    {
	      err = -ECANCELED;
	      goto end;
	    }

	  if (p < MICROFREAK_SAMPLE_BATCH_PACKETS)
	    {
	      op = 0x16;
	      len = MICROFREAK_WAVE_BLK_SHRT;
	    }
	  else
	    {
	      op = 0x17;
	      len = MICROFREAK_WAVE_BLK_LAST_SHRT;
	    }

	  gint i;
	  for (i = 0; i < len && total < input->len; i++)
	    {
	      *dst = GINT16_TO_LE (*src);
	      dst++;
	      src++;
	      total += sizeof (gint16);
	    }
	  for (; i < len; i++)
	    {
	      *dst = 0;
	      dst++;
	    }

	  microfreak_8bit_msg_to_midi_msg ((guint8 *) blk, midi_msg);
	  tx_msg = microfreak_get_msg (backend, op, midi_msg,
				       MICROFREAK_WAVE_MSG_SIZE);
	  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg,
					    control);
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

	  usleep (MICROFREAK_REST_TIME_LONG_US);
	}
    }

end:
  g_free (name);
  g_free (sanitized);
  usleep (MICROFREAK_REST_TIME_LONG_US);
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
microfreak_get_storage_stats (struct backend *backend, guint8 type,
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
  usleep (MICROFREAK_REST_TIME_LONG_US);
  return err;
}

static gint
microfreak_sample_load (const gchar *path, struct idata *sample,
			struct job_control *control)
{
  return common_sample_load (path, sample, control, MICROFREAK_SAMPLERATE, 1,
			     SF_FORMAT_PCM_16);
}

static const struct fs_operations FS_MICROFREAK_SAMPLE_OPERATIONS = {
  .id = FS_MICROFREAK_SAMPLE,
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
  .delete = microfreak_sample_clear,
  .clear = microfreak_sample_clear,
  .upload = microfreak_sample_upload,
  .load = microfreak_sample_load,
  .get_exts = backend_get_audio_exts,
  .get_upload_path = common_slot_get_upload_path
};

static gint
microfreak_next_wavetable_dentry (struct item_iterator *iter)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;
  struct microfreak_wavetable_header header;
  struct microfreak_iter_data *data = iter->data;
  guint8 id = data->next - 1;

  if (data->next > MICROFREAK_MAX_WAVETABLES)
    {
      return -ENOENT;
    }

  tx_msg = microfreak_get_wave_op_msg (data->backend, 0x57, id, 0, 0);	//Last byte is 1 in MIDI Control Center
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
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x16, MICROFREAK_WAVE_MSG_SIZE);
  if (err)
    {
      goto end;
    }

  microfreak_midi_msg_to_8bit_msg (MICROFREAK_GET_MSG_PAYLOAD (rx_msg),
				   (guint8 *) & header);
  snprintf (iter->item.name, LABEL_MAX, "%s", header.name);
  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = header.status0 == MICROFREAK_WAVETABLE_EMPTY ?
    0 : MICROFREAK_WAVETABLE_SIZE;
  (data->next)++;

end:
  free_msg (rx_msg);
  usleep (MICROFREAK_REST_TIME_LONG_US);
  return err;
}

static gint
microfreak_wavetable_read_dir (struct backend *backend,
			       struct item_iterator *iter, const gchar *path,
			       GSList *extensions)
{
  return microfreak_common_read_dir (backend, iter, path,
				     microfreak_next_wavetable_dentry);
}

static gint
microfreak_wavetable_load_sample (const gchar *path, struct idata *wavetable,
				  struct job_control *control)
{
  struct idata aux;
  gint err = common_sample_load (path, &aux, control, 0, 1,
				 SF_FORMAT_PCM_16);
  if (err)
    {
      return -EINVAL;
    }

  if (aux.content->len == MICROFREAK_WAVETABLE_SIZE)
    {
      idata_init (wavetable, idata_steal (&aux), NULL, NULL);
      return 0;
    }
  else
    {
      debug_print (1, "Resampling to get a valid wavetable...\n");
      GByteArray *resampled =
	g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);
      struct sample_info *sample_info = aux.info;
      gdouble r = MICROFREAK_WAVETABLE_LEN / (gdouble) sample_info->frames;
      err = sample_resample (wavetable->content, resampled,
			     sample_info->channels, r);
      if (err)
	{
	  g_byte_array_free (resampled, TRUE);
	}
      else
	{
	  sample_info->frames = MICROFREAK_WAVETABLE_LEN;
	  idata_init (wavetable, resampled, NULL, NULL);
	}

      idata_free (&aux);
      return err;
    }
}

static gint
microfreak_wavetable_download_part (struct backend *backend,
				    GByteArray *output,
				    struct job_control *control, guint8 id,
				    guint8 part)
{
  gint err;
  gint16 *src, *dst;
  guint8 msg_8bit[MICROFREAK_WAVE_BLK_SIZE];
  GByteArray *tx_msg, *rx_msg;

  tx_msg = microfreak_get_wave_op_msg (backend, 0x55, id, part, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return -err;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x15, 0);
  free_msg (rx_msg);
  if (err)
    {
      goto end;
    }

  usleep (MICROFREAK_REST_TIME_US);

  dst = (gint16 *) (output->data + part * MICROFREAK_SAMPLE_BATCH_SIZE);
  for (gint p = 1; p <= MICROFREAK_SAMPLE_BATCH_PACKETS; p++)
    {
      guint8 op;
      guint len;
      gboolean active;

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      if (!active)
	{
	  return -ECANCELED;
	}

      if (p < MICROFREAK_SAMPLE_BATCH_PACKETS)
	{
	  op = 0x16;
	  len = MICROFREAK_WAVE_BLK_SHRT;
	}
      else
	{
	  op = 0x17;
	  len = MICROFREAK_WAVE_BLK_LAST_SHRT;
	}

      tx_msg = microfreak_get_msg (backend, 0x18, "0x00", 1);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      if (err)
	{
	  return err;
	}
      err = MICROFREAK_CHECK_OP_LEN (rx_msg, op, 0x20);
      if (!err)
	{
	  microfreak_midi_msg_to_8bit_msg (MICROFREAK_GET_MSG_PAYLOAD
					   (rx_msg), msg_8bit);
	  src = (gint16 *) msg_8bit;
	  for (gint i = 0; i < len; i++)
	    {
	      gint16 v = GINT16_FROM_LE (*src);
	      memcpy ((guint8 *) dst, (guint8 *) & v, MICROFREAK_SAMPLE_SIZE);
	      dst++;
	      src++;
	    }
	}
      free_msg (rx_msg);
      if (err)
	{
	  return err;
	}

      usleep (MICROFREAK_REST_TIME_US);
    }

end:
  return err;
}

static gint
microfreak_wavetable_download (struct backend *backend, const gchar *path,
			       struct idata *wavetable,
			       struct job_control *control)
{
  guint id;
  guint err = 0;
  struct sample_info *sample_info;
  struct item_iterator iter;
  gboolean found;
  gchar name[MICROFREAK_WAVETABLE_NAME_LEN];
  GByteArray *content;

  err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  err = microfreak_wavetable_read_dir (backend, &iter, "/", NULL);
  if (err)
    {
      return err;
    }

  name[0] = 0;
  found = FALSE;
  while (!item_iterator_next (&iter))
    {
      if (iter.item.id - 1 == id)
	{
	  err = snprintf (name, MICROFREAK_WAVETABLE_NAME_LEN, "%s",
			  iter.item.name);
	  //If truncation happen, it is not marked as found and it will eventually lead to an error.
	  if (err < MICROFREAK_WAVETABLE_NAME_LEN)
	    {
	      found = TRUE;
	    }
	}
    }
  item_iterator_free (&iter);

  if (!found)
    {
      return -EINVAL;
    }

  sample_info = g_malloc (sizeof (struct sample_info));
  sample_info->format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sample_info->rate = MICROFREAK_SAMPLERATE;
  sample_info->loop_start = 0;
  sample_info->loop_end = MICROFREAK_WAVETABLE_LEN - 1;
  sample_info->loop_type = 0;
  sample_info->midi_note = 0;
  sample_info->channels = 1;

  content = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);
  content->len = MICROFREAK_WAVETABLE_SIZE;

  control->parts = (MICROFREAK_SAMPLE_BATCH_PACKETS + 1) *
    MICROFREAK_WAVETABLE_PARTS;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  err = 0;
  for (guint8 part = 0; part < MICROFREAK_WAVETABLE_PARTS && !err; part++)
    {
      err = microfreak_wavetable_download_part (backend, content, control, id,
						part);
    }

  if (err)
    {
      g_free (content);
      g_free (sample_info);
    }
  else
    {
      idata_init (wavetable, content, name, sample_info);
    }

  return err;
}

//This function is used to upload but also to clear up wavetables so it is not possible to have a control struct here.

static gint
microfreak_wavetable_upload_part (struct backend *backend, GByteArray *input,
				  guint8 id, guint8 part)
{
  gint err;
  gint16 *src, *dst;
  GByteArray *tx_msg, *rx_msg;
  guint8 msg_8bit[MICROFREAK_WAVE_BLK_SIZE];

  tx_msg = microfreak_get_wave_op_msg (backend, 0x54, id, part, 1);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
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
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  src = (gint16 *) (input->data + part * MICROFREAK_SAMPLE_BATCH_SIZE);
  for (gint p = 1; p <= MICROFREAK_SAMPLE_BATCH_PACKETS; p++)
    {
      guint8 op;
      guint len;

      if (p < MICROFREAK_SAMPLE_BATCH_PACKETS)
	{
	  op = 0x16;
	  len = MICROFREAK_WAVE_BLK_SHRT;
	}
      else
	{
	  op = 0x17;
	  len = MICROFREAK_WAVE_BLK_LAST_SHRT;
	}

      dst = (gint16 *) msg_8bit;
      for (gint i = 0; i < len; i++)
	{
	  gint16 v = GINT16_TO_LE (*src);
	  memcpy ((guint8 *) dst, (guint8 *) & v, MICROFREAK_SAMPLE_SIZE);
	  dst++;
	  src++;
	}

      tx_msg = microfreak_get_msg_from_8bit_msg (backend, op,
						 (guint8 *) & msg_8bit);
      rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  return -EIO;
	}
      err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
      free_msg (rx_msg);
      if (err)
	{
	  return err;
	}

      usleep (MICROFREAK_REST_TIME_US);
    }

  return 0;
}

static gint
microfreak_wavetable_reset (struct backend *backend, guint id,
			    struct microfreak_wavetable_header *header)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = microfreak_get_wave_op_msg (backend, 0x56, id, 0, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
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
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg_from_8bit_msg (backend, 0x16,
					     (guint8 *) header);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  tx_msg = microfreak_get_msg (backend, 0x17,
			       "\x00\x00\x00\x00\x00\x00\x00\x00", 8);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = MICROFREAK_CHECK_OP_LEN (rx_msg, 0x18, 0);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  usleep (MICROFREAK_REST_TIME_US);

  return err;
}

static gint
microfreak_wavetable_set_entry (struct backend *backend, guint id,
				const gchar *name, guint8 status)
{
  struct microfreak_wavetable_header header;
  memset (&header, 0, sizeof (header));
  header.id0 = id;
  header.id1 = id;
  header.status0 = status;
  header.status1 = 1;
  header.status2 = 1;
  snprintf (header.name, MICROFREAK_WAVETABLE_NAME_LEN, "%s", name);

  return microfreak_wavetable_reset (backend, id, &header);
}

static gint
microfreak_wavetable_rename (struct backend *backend, const gchar *src,
			     const gchar *dst)
{
  guint id;

  if (common_slot_get_id_name_from_path (src, &id, NULL))
    {
      return -EINVAL;
    }

  id--;
  if (id >= MICROFREAK_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  return microfreak_wavetable_set_entry (backend, id, dst, 0);
}

static gint
microfreak_wavetable_upload_id_name (struct backend *backend,
				     const gchar *path, GByteArray *wavetable,
				     struct job_control *control, guint id,
				     const gchar *name)
{
  gint err;

  id--;
  if (id >= MICROFREAK_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  control->parts = 1 + MICROFREAK_WAVETABLE_PARTS;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  err = microfreak_wavetable_set_entry (backend, id, name, 0);
  if (err)
    {
      return err;
    }

  control->part++;
  set_job_control_progress (control, 1.0);

  for (guint8 part = 0; part < MICROFREAK_WAVETABLE_PARTS && !err; part++)
    {
      gboolean active;

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      if (!active)
	{
	  return -ECANCELED;
	}

      err = microfreak_wavetable_upload_part (backend, wavetable, id, part);
      set_job_control_progress (control, 1.0);
      control->part++;
    }

  return err;
}

static gint
microfreak_xwavetable_upload (struct backend *backend, const gchar *path,
			      struct idata *idata,
			      struct job_control *control)
{
  gint err;
  guint id;
  GByteArray *wavetable = idata->content;

  err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  return microfreak_wavetable_upload_id_name (backend, path, wavetable,
					      control, id, idata->name);
}

static gint
microfreak_wavetable_upload (struct backend *backend, const gchar *path,
			     struct idata *idata, struct job_control *control)
{
  gint err;
  guint id;
  gchar *name;
  GByteArray *wavetable = idata->content;

  err = common_slot_get_id_name_from_path (path, &id, &name);
  if (err)
    {
      return err;
    }

  err = microfreak_wavetable_upload_id_name (backend, path, wavetable,
					     control, id, name);
  g_free (name);
  return err;
}

static gint
microfreak_wavetable_clear (struct backend *backend, const gchar *path)
{
  gint err;
  guint id;
  GByteArray *data;

  err = common_slot_get_id_name_from_path (path, &id, NULL);
  if (err)
    {
      return err;
    }

  id--;
  if (id >= MICROFREAK_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  err = microfreak_wavetable_set_entry (backend, id, "",
					MICROFREAK_WAVETABLE_EMPTY);
  if (err)
    {
      return err;
    }

  data = g_byte_array_sized_new (MICROFREAK_SAMPLE_BATCH_SIZE);
  memset (data->data, 0, MICROFREAK_SAMPLE_BATCH_SIZE);

  for (guint8 part = 0; part < MICROFREAK_WAVETABLE_PARTS && !err; part++)
    {
      err = microfreak_wavetable_upload_part (backend, data, id, part);
    }

  g_byte_array_free (data, TRUE);

  return err;
}

static gchar *
microfreak_get_download_wavetable_path (struct backend *backend,
					const struct fs_operations *ops,
					const gchar *dst_dir,
					const gchar *src_path,
					struct idata *wavetable)
{
  guint id;

  if (!wavetable)
    {
      return NULL;
    }

  if (common_slot_get_id_name_from_path (src_path, &id, NULL))
    {
      return NULL;
    }

  return common_get_download_path_with_params (backend, ops, dst_dir, id, 2,
					       wavetable->name);
}

gint
microfreak_deserialize_wavetable (struct idata *wavetable,
				  struct idata *serialized)
{
  gchar name[MICROFREAK_WAVETABLE_NAME_LEN];
  guint8 p0, p3, p5;
  guint datalen;
  gint err;
  GByteArray *data = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);

  data->len = MICROFREAK_WAVETABLE_SIZE;
  err = microfreak_deserialize_object (serialized->content,
				       MICROFREAK_WAVETABLE_HEADER,
				       sizeof (MICROFREAK_WAVETABLE_HEADER) -
				       1, name, &p0, &p3, &p5,
				       data->data, &datalen);
  if (datalen == MICROFREAK_WAVETABLE_SIZE)
    {
      idata_init (wavetable, data, name, NULL);
    }
  else
    {
      g_byte_array_free (wavetable->content, TRUE);
      err = -EINVAL;
    }

  return err;
}

gint
microfreak_serialize_wavetable (struct idata *serialized,
				struct idata *wavetable)
{
  gint err;
  GByteArray *data = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE * 8);

  err = microfreak_serialize_object (data, MICROFREAK_WAVETABLE_HEADER,
				     sizeof (MICROFREAK_WAVETABLE_HEADER) -
				     1, wavetable->name, 1, 0, 1,
				     wavetable->content->data,
				     MICROFREAK_WAVETABLE_SIZE);
  if (err)
    {
      g_byte_array_free (data, TRUE);
    }
  else
    {
      idata_init (serialized, data, NULL, NULL);
    }

  return err;
}

static gint
microfreak_pwavetable_load (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = file_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_wavetable (wavetable, &aux);

  idata_free (&aux);
  return err;
}

static gint
microfreak_pwavetable_save (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_serialize_wavetable (&aux, wavetable);
  if (err)
    {
      goto cleanup;
    }

  err = file_save (path, &aux, control);

cleanup:
  idata_free (&aux);
  return err;
}

static gchar *
microfreak_get_wavetable_id_as_slot (struct item *item,
				     struct backend *backend)
{
  return common_get_id_as_slot_padded (item, backend, 2);
}

static const struct fs_operations FS_MICROFREAK_PWAVETABLE_OPERATIONS = {
  .id = FS_MICROFREAK_PWAVETABLE,
  .options = FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE,
  .name = "pwavetable",
  .ext = MICROFREAK_PWAVETABLE_EXT,
  .max_name_len = MICROFREAK_WAVETABLE_NAME_LEN - 1,
  .readdir = microfreak_wavetable_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_wavetable_id_as_slot,
  .clear = microfreak_wavetable_clear,
  .rename = microfreak_wavetable_rename,
  .download = microfreak_wavetable_download,
  .upload = microfreak_xwavetable_upload,
  .load = microfreak_pwavetable_load,
  .save = microfreak_pwavetable_save,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_wavetable_path
};

static gint
microfreak_zwavetable_load (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_zobject_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_wavetable (wavetable, &aux);

  idata_free (&aux);
  return err;
}

static gint
microfreak_zwavetable_save (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_serialize_wavetable (&aux, wavetable);
  if (err)
    {
      goto cleanup;
    }

  err = microfreak_zobject_save (path, &aux, control, "0_wavetable");

cleanup:
  idata_free (&aux);
  return err;
}

static const struct fs_operations FS_MICROFREAK_ZWAVETABLE_OPERATIONS = {
  .id = FS_MICROFREAK_ZWAVETABLE,
  .options = FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE,
  .name = "zwavetable",
  .ext = MICROFREAK_ZWAVETABLE_EXT,
  .max_name_len = MICROFREAK_WAVETABLE_NAME_LEN - 1,
  .readdir = microfreak_wavetable_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_wavetable_id_as_slot,
  .clear = microfreak_wavetable_clear,
  .rename = microfreak_wavetable_rename,
  .download = microfreak_wavetable_download,
  .upload = microfreak_xwavetable_upload,
  .load = microfreak_zwavetable_load,
  .save = microfreak_zwavetable_save,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_wavetable_path
};

static gint
microfreak_wavetable_load (const gchar *path, struct idata *wavetable,
			   struct job_control *control)
{
  const gchar *ext = filename_get_ext (path);
  if (ext != NULL && strcmp (ext, MICROFREAK_PWAVETABLE_EXT) == 0)
    {
      return microfreak_pwavetable_load (path, wavetable, control);
    }
  else if (ext != NULL && strcmp (ext, MICROFREAK_ZWAVETABLE_EXT) == 0)
    {
      return microfreak_zwavetable_load (path, wavetable, control);
    }
  else
    {
      return microfreak_wavetable_load_sample (path, wavetable, control);
    }
}

static gint
microfreak_wavetable_save (const gchar *path, struct idata *wavetable,
			   struct job_control *control)
{
  return sample_save_to_file (path, wavetable, control, SF_FORMAT_WAV |
			      SF_FORMAT_PCM_16);
}

static GSList *
microfreak_wavetable_get_exts (struct backend *backend,
			       const struct fs_operations *ops)
{
  GSList *exts = backend_get_audio_exts (backend, ops);
  exts = g_slist_append (exts, strdup (MICROFREAK_PWAVETABLE_EXT));
  return g_slist_append (exts, strdup (MICROFREAK_ZWAVETABLE_EXT));
}

static const struct fs_operations FS_MICROFREAK_WAVETABLE_OPERATIONS = {
  .id = FS_MICROFREAK_WAVETABLE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_SHOW_SIZE_COLUMN |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wavetable",
  .gui_name = "Wavetables",
  .gui_icon = BE_FILE_ICON_WAVE,
  .ext = "wav",
  .max_name_len = MICROFREAK_WAVETABLE_NAME_LEN - 1,
  .readdir = microfreak_wavetable_read_dir,
  .print_item = common_print_item,
  .get_slot = microfreak_get_wavetable_id_as_slot,
  .delete = microfreak_wavetable_clear,
  .clear = microfreak_wavetable_clear,
  .rename = microfreak_wavetable_rename,
  .download = microfreak_wavetable_download,
  .upload = microfreak_wavetable_upload,
  .load = microfreak_wavetable_load,
  .save = microfreak_wavetable_save,
  .get_exts = microfreak_wavetable_get_exts,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microfreak_get_download_wavetable_path
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
      error_print ("MicroFreak firmware version 5 required");
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
      tx_msg = microfreak_get_preset_op_msg (backend, 0x19, 0, 0);
      rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
      if (!rx_msg)
	{
	  g_free (backend->data);
	  backend->data = NULL;
	  return -ENODEV;
	}
      free_msg (rx_msg);
      usleep (MICROFREAK_REST_TIME_LONG_US);
      backend_midi_handshake (backend);
      err = microfreak_handshake_int (backend);
      if (err)
	{
	  g_free (backend->data);
	  backend->data = NULL;
	  return err;
	}
    }

  backend_fill_fs_ops (backend, &FS_MICROFREAK_PPRESET_OPERATIONS,
		       &FS_MICROFREAK_ZPRESET_OPERATIONS,
		       &FS_MICROFREAK_PRESET_OPERATIONS,
		       &FS_MICROFREAK_SAMPLE_OPERATIONS,
		       &FS_MICROFREAK_PWAVETABLE_OPERATIONS,
		       &FS_MICROFREAK_ZWAVETABLE_OPERATIONS,
		       &FS_MICROFREAK_WAVETABLE_OPERATIONS, NULL);
  backend->destroy_data = backend_destroy_data;
  backend->get_storage_stats = microfreak_get_storage_stats;

  snprintf (backend->name, LABEL_MAX, "Arturia MicroFreak");

  return 0;
}
