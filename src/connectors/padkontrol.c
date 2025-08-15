/*
 *   padkontrol.c
 *   Copyright (C) 2025 David García Goñi <dagargo@gmail.com>
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
#include "microbrute.h"
#include "common.h"

#define PADKONTROL_MAX_SCENES 16

enum padkontrol_fs
{
  FS_PADKONTROL_SCENE
};

static const guint8 KORG_ID[] = { 0x42, 0x0, 0x0 };
static const guint8 FAMILY_ID[] = { 0x6e, 0x0 };
static const guint8 MODEL_ID[] = { 0x8, 0x0 };

#define PADKONTROL_MSG_FUNC(msg) (msg->data[6])
#define PADKONTROL_MSG_PAYLOAD(msg) (msg->data[7])

static const guint8 PADKONTROL_MSG[] =
  { 0xf0, 0x42, 0x40, 0x6e, 0x08, 0x1f, 0, 0, 0xf7 };

#define PADKONTROL_FUNC_SCENE_CHANGE_OP 0x14
#define PADKONTROL_FUNC_CURRENT_SCENE_DUMP_OP 0x10
#define PADKONTROL_FUNC_SCENE_WRITE_OP 0x11
#define PADKONTROL_FUNC_WRITE_COMPLETED 0x21
#define PADKONTROL_FUNC_LOAD_COMPLETE_ACK 0x23
#define PADKONTROL_FUNC_SCENE_CHANGE 0x4f

#define PADKONTROL_MSG_SIZE sizeof(PADKONTROL_MSG)
#define PADKONTROL_SCENE_SIZE 150

#define PADKONTROL_REST_TIME_US 10000

static gint
padkontrol_next_dentry (struct item_iterator *iter)
{
  gint err = common_simple_next_dentry (iter);
  iter->item.size = PADKONTROL_SCENE_SIZE;
  return err;
}

static gint
padkontrol_read_dir (struct backend *backend, struct item_iterator *iter,
		     const gchar *dir, const gchar **extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 1;
  data->last = PADKONTROL_MAX_SCENES;

  item_iterator_init (iter, dir, data, padkontrol_next_dentry, g_free);

  return 0;
}

static GByteArray *
padkontrol_get_msg (guint8 op, guint8 payload)
{
  GByteArray *tx_msg;
  tx_msg = g_byte_array_sized_new (16);
  g_byte_array_append (tx_msg, PADKONTROL_MSG, sizeof (PADKONTROL_MSG));
  PADKONTROL_MSG_FUNC (tx_msg) = op;
  PADKONTROL_MSG_PAYLOAD (tx_msg) = payload;
  return tx_msg;
}

static gint
padkontrol_download (struct backend *backend, const gchar *path,
		     struct idata *scene, struct job_control *control)
{
  guint id;
  gint err;
  GByteArray *tx_msg, *rx_msg;

  job_control_reset (control, 3);

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }
  if (id < 1 || id > PADKONTROL_MAX_SCENES)
    {
      return -EINVAL;
    }

  id--;				// O based
  tx_msg = padkontrol_get_msg (PADKONTROL_FUNC_SCENE_CHANGE_OP, id);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != PADKONTROL_MSG_SIZE ||
      PADKONTROL_MSG_FUNC (rx_msg) != PADKONTROL_FUNC_SCENE_CHANGE)
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  free_msg (rx_msg);

  common_data_tx_and_rx_part (backend, NULL, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != PADKONTROL_MSG_SIZE ||
      PADKONTROL_MSG_FUNC (rx_msg) != PADKONTROL_FUNC_LOAD_COMPLETE_ACK)
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  free_msg (rx_msg);

  usleep (PADKONTROL_REST_TIME_US);

  tx_msg = padkontrol_get_msg (PADKONTROL_FUNC_CURRENT_SCENE_DUMP_OP, 0);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != PADKONTROL_SCENE_SIZE)
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  idata_init (scene, rx_msg, NULL, NULL);

  usleep (PADKONTROL_REST_TIME_US);

  return 0;
}

static gint
padkontrol_upload (struct backend *backend, const gchar *path,
		   struct idata *scene, struct job_control *control)
{
  guint id;
  gint err;
  GByteArray *tx_msg, *rx_msg;

  job_control_reset (control, 2);

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }
  if (id < 1 || id > PADKONTROL_MAX_SCENES)
    {
      return -EINVAL;
    }

  tx_msg = scene->content;
  scene->content = NULL;
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != PADKONTROL_MSG_SIZE ||
      PADKONTROL_MSG_FUNC (rx_msg) != PADKONTROL_FUNC_LOAD_COMPLETE_ACK)
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  usleep (PADKONTROL_REST_TIME_US);

  id--;				// O based
  tx_msg = padkontrol_get_msg (PADKONTROL_FUNC_SCENE_WRITE_OP, id);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != PADKONTROL_MSG_SIZE ||
      PADKONTROL_MSG_FUNC (rx_msg) != PADKONTROL_FUNC_WRITE_COMPLETED)
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  free_msg (rx_msg);

  usleep (PADKONTROL_REST_TIME_US);

  return 0;
}

static gchar *
padkontrol_get_id_as_slot (struct item *item, struct backend *backend)
{
  gchar *slot = g_malloc (LABEL_MAX);
  snprintf (slot, LABEL_MAX, "%02d", item->id);
  return slot;
}

static const struct fs_operations FS_PADKONTROL_OPERATIONS = {
  .id = FS_PADKONTROL_SCENE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "scene",
  .gui_name = "Scenes",
  .gui_icon = FS_ICON_SND,
  .readdir = padkontrol_read_dir,
  .print_item = common_print_item,
  .download = padkontrol_download,
  .upload = padkontrol_upload,
  .get_slot = padkontrol_get_id_as_slot,
  .load = file_load,
  .save = file_save,
  .get_exts = common_sysex_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nn
};

static gint
padkontrol_handshake (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, KORG_ID, sizeof (KORG_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  gslist_fill (&backend->fs_ops, &FS_PADKONTROL_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "KORG padKONTROL");

  return 0;
}

const struct connector CONNECTOR_PADKONTROL = {
  .name = "padkontrol",
  .handshake = padkontrol_handshake,
  .options = 0,
  .regex = ".*padKONTROL.*"
};
