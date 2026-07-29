/*
 *   monomachine.c
 *   Copyright (C) 2026 Ian Hundere
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

//DigiPRO waveform protocol notes (verified against a Monomachine MKII, OS 1.32):
//
//A waveform request is 'F0 00 20 3C 03 00 5E pos F7' with pos in [0, 63]. If
//the slot is in use, the device answers with a waveform dump from any screen;
//if the slot is empty, the device stays silent.
//
//A waveform dump is always 7027 bytes.
//
//  offset  length  content
//  0       7       F0 00 20 3C 03 00 5D
//  7       2       version (01 01)
//  9       1       waveform slot (0 based)
//  10      4       name (ASCII, no terminator)
//  14      7008    876 blocks of 8 bytes; each block encodes 7 data bytes
//                  preceded by a byte carrying their MSBs (bit 6 for the
//                  first data byte, bit 0 for the last)
//  7022    2       sum of bytes [10, 7022) & 0x3FFF as two septets
//  7024    2       7017 (length of bytes [10, 7027)) as two septets
//  7026    1       F7
//
//The decoded 6132 bytes are 2044 frames of signed 24 bit big endian mono PCM
//(a single cycle waveform). Only the 16 most significant bits are used here.
//
//The device only stores incoming dumps while "RECEIVE" is armed in the
//DIGIPRO MGR menu ("WAITING..." on screen). With a slot selected, the
//incoming waveform is stored there; with "ORG" selected, the slot byte of
//the dump is used. Dumps sent outside of this state are discarded and dumps
//are never acknowledged, so uploads require user interaction.

#include <glib/gi18n.h>
#include "monomachine.h"
#include "common.h"
#include "sample.h"

#define MONOMACHINE_SAMPLE_TOTAL 64
#define MONOMACHINE_SAMPLE_NAME_LEN 4
#define MONOMACHINE_WAVE_FRAMES 2044
#define MONOMACHINE_WAVE_RAW_LEN (MONOMACHINE_WAVE_FRAMES * 3)
#define MONOMACHINE_WAVE_MSG_LEN 7027
#define MONOMACHINE_WAVE_SLOT_OFFSET 9
#define MONOMACHINE_WAVE_NAME_OFFSET 10
#define MONOMACHINE_WAVE_DATA_OFFSET 14
#define MONOMACHINE_WAVE_CKSUM_OFFSET 7022
//Nominal rate; the device plays waveforms as wavetable cycles.
#define MONOMACHINE_SAMPLE_RATE 44100

static const guint8 MONOMACHINE_GLOBAL_SETTINGS_REQUEST[] =
  { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x51, 0, 0xf7 };

static const guint8 MONOMACHINE_WAVEFORM_REQUEST[] =
  { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x5e, 0, 0xf7 };

static const guint8 MONOMACHINE_WAVEFORM_DUMP_HEADER[] =
  { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x5d, 1, 1 };

enum monomachine_fs
{
  FS_WAVEFORM_MONOMACHINE = 1
};

//Checks the SysEx start, the manufacturer ID, the product ID (bytes 0 to 4)
//and the message ID (byte 6). Byte 5 is the base channel, which might not be
//zero, so it is not compared.
static gboolean
monomachine_check_header (GByteArray *msg, guint8 msg_id)
{
  return msg->len >= 7 &&
    !memcmp (msg->data, MONOMACHINE_WAVEFORM_DUMP_HEADER, 5) &&
    msg->data[6] == msg_id;
}

guint16
monomachine_wave_msg_checksum (const guint8 *msg)
{
  guint32 cksum = 0;

  for (guint i = MONOMACHINE_WAVE_NAME_OFFSET;
       i < MONOMACHINE_WAVE_CKSUM_OFFSET; i++)
    {
      cksum += msg[i];
    }

  return cksum & 0x3fff;
}

//Fills dst with the first MONOMACHINE_SAMPLE_NAME_LEN characters of src
//uppercased, restricted to printable ASCII and padded with spaces.
void
monomachine_sanitize_name (const gchar *src,
			   gchar dst[MONOMACHINE_SAMPLE_NAME_LEN + 1])
{
  guint len = src ? strlen (src) : 0;

  for (guint i = 0; i < MONOMACHINE_SAMPLE_NAME_LEN; i++)
    {
      gchar c = i < len ? g_ascii_toupper (src[i]) : ' ';
      dst[i] = c < 0x20 || c >= 0x7f ? ' ' : c;
    }
  dst[MONOMACHINE_SAMPLE_NAME_LEN] = 0;
}

//Builds a waveform dump for slot id (0 based) from 16 bit mono frames,
//truncating or zero padding to the fixed waveform length.
GByteArray *
monomachine_wave_msg_new (guint id, struct idata *sample)
{
  guint16 cksum;
  guint frames;
  gint16 *frame;
  guint8 trailer[5], pos, raw[MONOMACHINE_WAVE_RAW_LEN];
  gchar name[MONOMACHINE_SAMPLE_NAME_LEN + 1];
  struct sample_info *sample_info = sample->info;
  GByteArray *tx_msg = g_byte_array_sized_new (MONOMACHINE_WAVE_MSG_LEN);

  memset (raw, 0, MONOMACHINE_WAVE_RAW_LEN);
  frames = MIN (sample_info->frames, sample->content->len / sizeof (gint16));
  frames = MIN (frames, MONOMACHINE_WAVE_FRAMES);
  frame = (gint16 *) sample->content->data;
  for (guint i = 0; i < frames; i++, frame++)
    {
      raw[i * 3] = (*frame >> 8) & 0xff;
      raw[i * 3 + 1] = *frame & 0xff;
    }

  g_byte_array_append (tx_msg, MONOMACHINE_WAVEFORM_DUMP_HEADER,
		       sizeof (MONOMACHINE_WAVEFORM_DUMP_HEADER));
  pos = id;
  g_byte_array_append (tx_msg, &pos, 1);
  monomachine_sanitize_name (sample->name, name);
  g_byte_array_append (tx_msg, (guint8 *) name, MONOMACHINE_SAMPLE_NAME_LEN);

  for (guint i = 0; i < MONOMACHINE_WAVE_RAW_LEN; i += 7)
    {
      guint8 block[8];

      block[0] = 0;
      for (guint j = 0; j < 7; j++)
	{
	  if (raw[i + j] & 0x80)
	    {
	      block[0] |= 0x40 >> j;
	    }
	  block[1 + j] = raw[i + j] & 0x7f;
	}
      g_byte_array_append (tx_msg, block, 8);
    }

  cksum = monomachine_wave_msg_checksum (tx_msg->data);
  trailer[0] = cksum >> 7;
  trailer[1] = cksum & 0x7f;
  //Length of bytes [10, 7027) as two septets.
  trailer[2] = (MONOMACHINE_WAVE_MSG_LEN - MONOMACHINE_WAVE_NAME_OFFSET) >> 7;
  trailer[3] = (MONOMACHINE_WAVE_MSG_LEN - MONOMACHINE_WAVE_NAME_OFFSET) &
    0x7f;
  trailer[4] = 0xf7;
  g_byte_array_append (tx_msg, trailer, 5);

  return tx_msg;
}

//Validates a waveform dump for slot id (0 based) and extracts the name and
//the frames into sample.
gint
monomachine_wave_msg_parse (GByteArray *rx_msg, guint id,
			    struct idata *sample)
{
  gchar *name;
  gint16 frame;
  GByteArray *output;
  struct sample_info *sample_info;
  guint8 *packed, raw[MONOMACHINE_WAVE_RAW_LEN];

  if (rx_msg->len != MONOMACHINE_WAVE_MSG_LEN ||
      !monomachine_check_header (rx_msg, 0x5d) ||
      rx_msg->data[MONOMACHINE_WAVE_SLOT_OFFSET] != id)
    {
      debug_print (1, "Bad waveform message (%u bytes)", rx_msg->len);
      return -EIO;
    }

  if (monomachine_wave_msg_checksum (rx_msg->data) !=
      ((rx_msg->data[MONOMACHINE_WAVE_CKSUM_OFFSET] << 7) |
       rx_msg->data[MONOMACHINE_WAVE_CKSUM_OFFSET + 1]))
    {
      debug_print (1, "Bad waveform checksum");
      return -EIO;
    }

  //3 byte frames straddle the 8 byte blocks, so the payload is fully
  //decoded first.
  packed = &rx_msg->data[MONOMACHINE_WAVE_DATA_OFFSET];
  for (guint i = 0, j = 0; j < MONOMACHINE_WAVE_RAW_LEN; i += 8)
    {
      for (guint k = 0; k < 7 && j < MONOMACHINE_WAVE_RAW_LEN; k++, j++)
	{
	  raw[j] = packed[i + 1 + k] | (packed[i] & (0x40 >> k) ? 0x80 : 0);
	}
    }

  output = g_byte_array_sized_new (MONOMACHINE_WAVE_FRAMES * sizeof (gint16));
  for (guint i = 0; i < MONOMACHINE_WAVE_FRAMES; i++)
    {
      frame = (gint16) ((raw[i * 3] << 8) | raw[i * 3 + 1]);
      g_byte_array_append (output, (guint8 *) & frame, sizeof (gint16));
    }

  sample_info = sample_info_new (FALSE);
  sample_info->frames = MONOMACHINE_WAVE_FRAMES;
  sample_info->rate = MONOMACHINE_SAMPLE_RATE;
  sample_info->channels = 1;
  sample_info->format = SF_FORMAT_PCM_16;
  sample_info->loop_start = 0;
  sample_info->loop_end = MONOMACHINE_WAVE_FRAMES - 1;

  name = g_strndup ((gchar *) & rx_msg->data[MONOMACHINE_WAVE_NAME_OFFSET],
		    MONOMACHINE_SAMPLE_NAME_LEN);
  g_strchomp (name);

  idata_init (sample, output, name, sample_info, sample_info_free);

  return 0;
}

static gint
monomachine_get_slot_id_from_path (const gchar *path, guint *id)
{
  gint err = common_slot_get_id_from_path (path, id);

  if (err)
    {
      return err;
    }

  return *id >= 1 && *id <= MONOMACHINE_SAMPLE_TOTAL ? 0 : -EINVAL;
}

static gint
monomachine_read_dir (struct backend *backend, struct item_iterator *iter,
		      const gchar *dir, const gchar **extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 1;
  data->last = MONOMACHINE_SAMPLE_TOTAL;

  item_iterator_init (iter, dir, data, common_simple_next_dentry, g_free);

  return 0;
}

static gint
monomachine_download (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;
  GByteArray *tx_msg, *rx_msg;

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  task_control_reset (control, 1);

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

  tx_msg = g_byte_array_sized_new (sizeof (MONOMACHINE_WAVEFORM_REQUEST));
  g_byte_array_append (tx_msg, MONOMACHINE_WAVEFORM_REQUEST,
		       sizeof (MONOMACHINE_WAVEFORM_REQUEST));
  tx_msg->data[7] = id - 1;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, BE_SYSEX_TIMEOUT_MS);
  if (!rx_msg)
    {
      //Empty slots do not answer.
      return -ENODATA;
    }

  task_control_set_progress (control, 0.5);

  err = monomachine_wave_msg_parse (rx_msg, id - 1, sample);

  free_msg (rx_msg);
  task_control_set_progress (control, 1.0);

  return err;
}

static gint
monomachine_upload (struct backend *backend, const gchar *path,
		    struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;
  GByteArray *tx_msg;

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  task_control_reset (control, 1);

  tx_msg = monomachine_wave_msg_new (id - 1, sample);
  //The device never acknowledges the dump, so this is a blind transfer.
  err = backend_tx (backend, tx_msg);

  task_control_set_progress (control, 1.0);

  return err;
}

static gint
monomachine_ask_upload (struct backend *backend, const gchar *path,
			struct idata *sample, struct task_control *control)
{
  gint err;
  guint id, printed;
  gchar msg[LABEL_MAX];
  gchar name[MONOMACHINE_SAMPLE_NAME_LEN + 1];
  struct sample_info *sample_info = sample->info;

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  monomachine_sanitize_name (sample->name, name);
  printed = snprintf (msg, LABEL_MAX,
		      _
		      ("Sending waveform “%s” to slot %u. In the DIGIPRO MGR menu, select “RECEIVE” and this slot (or “ORG”), press [ENTER/YES] and, when the transfer ends, [EXIT/NO]."),
		      name, id);
  if (sample_info->frames > MONOMACHINE_WAVE_FRAMES && printed < LABEL_MAX)
    {
      snprintf (&msg[printed], LABEL_MAX - printed, " %s",
		_("Only the first 2044 frames are used."));
    }

  if (elektroid_ask_user_to_continue (msg, &control->controllable))
    {
      return monomachine_upload (backend, path, sample, control);
    }
  else
    {
      return -ECANCELED;
    }
}

static gint
monomachine_sample_load (struct backend *backend, const gchar *path,
			 struct idata *sample, struct task_control *control)
{
  return common_sample_load (path, sample, control, 1, 0, SF_FORMAT_PCM_16,
			     FALSE);
}

static gint
monomachine_sample_save (struct backend *backend, const gchar *path,
			 struct idata *sample, struct task_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static const struct fs_operations FS_MONOMACHINE_SAMPLE_OPERATIONS = {
  .id = FS_WAVEFORM_MONOMACHINE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_ID_COLUMN,
  .name = "waveform",
  .gui_name = "Waveforms",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = MONOMACHINE_SAMPLE_NAME_LEN,
  .readdir = monomachine_read_dir,
  .download = monomachine_download,
  .upload = monomachine_ask_upload,
  .load = monomachine_sample_load,
  .save = monomachine_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nnn
};

static gint
monomachine_handshake (struct backend *backend)
{
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;

  tx_msg =
    g_byte_array_sized_new (sizeof (MONOMACHINE_GLOBAL_SETTINGS_REQUEST));
  g_byte_array_append (tx_msg, MONOMACHINE_GLOBAL_SETTINGS_REQUEST,
		       sizeof (MONOMACHINE_GLOBAL_SETTINGS_REQUEST));
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
				    BE_SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -ENODEV;
    }
  //The response length is not documented (101 bytes on OS 1.32), so only
  //the header is checked.
  if (!monomachine_check_header (rx_msg, 0x50))
    {
      err = -ENODEV;
      goto end;
    }

  gslist_fill (&backend->fs_ops, &FS_MONOMACHINE_SAMPLE_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "Elektron Monomachine");

end:
  free_msg (rx_msg);
  return err;
}

const struct connector CONNECTOR_MONOMACHINE = {
  .name = "monomachine",
  .handshake = monomachine_handshake,
  .options = CONNECTOR_OPTION_CUSTOM_HANDSHAKE,
  .regex = NULL
};
