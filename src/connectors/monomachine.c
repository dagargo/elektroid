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

#include <glib/gi18n.h>
#include "monomachine.h"
#include "common.h"
#include "sds.h"

//A Digibank holds 64 user replaceable waveforms (Monomachine manual OS 1.32, "DIGIPRO MANAGER").
#define MONOMACHINE_SAMPLE_TOTAL 64
#define MONOMACHINE_SAMPLE_NAME_MAX_LEN 16
//Inherited from the Machinedrum connector; not yet validated on Monomachine hardware.
#define MONOMACHINE_REST_TIME_DEFAULT 20000

//Global settings dump request as documented in appendix C of the Monomachine
//manual (OS 1.32): F0 00 20 3C 03 00 51 <slot> F7. The device answers with a
//global settings dump: F0 00 20 3C 03 00 50 ...
static const guint8 MONOMACHINE_GLOBAL_SETTINGS_REQUEST[] =
  { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x51, 0, 0xf7 };

static const guint8 MONOMACHINE_GLOBAL_SETTINGS_RESPONSE[] =
  { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x50 };

enum monomachine_fs
{
  FS_WAVEFORM_MONOMACHINE = 1
};

static gint
monomachine_read_dir (struct backend *backend, struct item_iterator *iter,
		      const gchar *dir, const gchar **extensions)
{
  struct sds_iterator_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct sds_iterator_data));
  data->next = 1;
  data->last = MONOMACHINE_SAMPLE_TOTAL;
  data->backend = backend;

  item_iterator_init (iter, dir, data, sds_next_sample_dentry, g_free);

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
monomachine_upload (struct backend *backend, const gchar *path,
		    struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  //16 is the bit depth, not the name length.
  return sds_upload_by_id (backend, id - 1, sample, control, 16);
}

static gint
monomachine_ask_upload (struct backend *backend, const gchar *path,
			struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;
  gchar msg[LABEL_MAX];

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  snprintf (msg, LABEL_MAX,
	    _
	    ("Sending waveform “%.16s” to slot %u. Select “RECEIVE” in the DIGIPRO MGR menu."),
	    sample->name, id);

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
monomachine_download (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  return sds_download_by_id (backend, id - 1, sample, control);
}

static gint
monomachine_ask_download (struct backend *backend, const gchar *path,
			  struct idata *sample, struct task_control *control)
{
  gint err;
  guint id;
  gchar msg[LABEL_MAX];

  err = monomachine_get_slot_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  snprintf (msg, LABEL_MAX,
	    _
	    ("Receiving waveform from slot %u. Send it with “SEND” in the DIGIPRO MGR menu."),
	    id);

  if (elektroid_ask_user_to_continue (msg, &control->controllable))
    {
      return monomachine_download (backend, path, sample, control);
    }
  else
    {
      return -ECANCELED;
    }
}

static const struct fs_operations FS_MONOMACHINE_SAMPLE_OPERATIONS = {
  .id = FS_WAVEFORM_MONOMACHINE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_ID_COLUMN,
  .name = "waveform",
  .gui_name = "Waveforms",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = MONOMACHINE_SAMPLE_NAME_MAX_LEN,
  .readdir = monomachine_read_dir,
  .download = monomachine_ask_download,
  .upload = monomachine_ask_upload,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nnn
};

static gint
monomachine_handshake (struct backend *backend)
{
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;
  struct sds_data *sds_data;

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
  //The response length is not documented, so only the header is checked:
  //bytes 0 to 4 (SysEx start, manufacturer ID and product ID) and byte 6
  //(the global settings dump ID). Byte 5 is the base channel, which might
  //not be zero, so it is not compared.
  if (rx_msg->len < sizeof (MONOMACHINE_GLOBAL_SETTINGS_RESPONSE) ||
      memcmp (rx_msg->data, MONOMACHINE_GLOBAL_SETTINGS_RESPONSE, 5) ||
      rx_msg->data[6] != MONOMACHINE_GLOBAL_SETTINGS_RESPONSE[6])
    {
      err = -ENODEV;
      goto end;
    }

  sds_data = g_malloc (sizeof (struct sds_data));
  sds_data->rest_time = MONOMACHINE_REST_TIME_DEFAULT;
  //It is unknown if the Monomachine implements the SDS name extension.
  //Unanswered name requests would make listing wait several seconds per slot.
  sds_data->name_extension = FALSE;

  gslist_fill (&backend->fs_ops, &FS_MONOMACHINE_SAMPLE_OPERATIONS, NULL);
  backend->data = sds_data;
  backend->destroy_data = backend_destroy_data;
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
