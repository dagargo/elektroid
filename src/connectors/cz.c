/*
 *   cz.c
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#include "cz.h"

#define CZ101_PROGRAM_LEN 263
#define CZ101_MAX_PROGRAMS 16

static const guint8 CZ_PROGRAM_REQUEST[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x00, 0x70, 0x31 };

enum cz_fs
{
  FS_PROGRAM_CZ = 1
};

struct cz_type_iterator_data
{
  guint next;
  guint offset;
};

static void
cz_free_iterator_data (void *iter_data)
{
  g_free (iter_data);
}

static const char *CZ_ROOT_DIRS[] = { "preset", "internal", "cartridge" };

static guint
cz_next_dentry_root (struct item_iterator *iter)
{
  gint next = *((gint *) iter->data);

  if (next < 3)
    {
      iter->item.index = next;
      snprintf (iter->item.name, LABEL_MAX, "%s", CZ_ROOT_DIRS[next]);
      iter->item.type = ELEKTROID_DIR;
      iter->item.size = -1;
      (*((gint *) iter->data))++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static guint
cz_next_dentry (struct item_iterator *iter)
{
  struct cz_type_iterator_data *data = iter->data;

  if (data->next < CZ101_MAX_PROGRAMS)
    {
      iter->item.index = data->next + data->offset;
      snprintf (iter->item.name, LABEL_MAX, "%d", data->next + 1);
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = CZ101_PROGRAM_LEN;
      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
cz_read_dir (struct backend *backend, struct item_iterator *iter,
	     const gchar * path)
{
  gint offset;

  if (!strcmp (path, "/"))
    {
      iter->data = g_malloc (sizeof (guint));
      *((gint *) iter->data) = 0;
      iter->next = cz_next_dentry_root;
      iter->free = cz_free_iterator_data;
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = -1;
      return 0;
    }
  else if ((strcmp (&path[1], CZ_ROOT_DIRS[0]) && (offset = 0)) ||
	   (strcmp (&path[1], CZ_ROOT_DIRS[1]) && (offset = 20)) ||
	   (strcmp (&path[1], CZ_ROOT_DIRS[2]) && (offset = 40)))
    {
      struct cz_type_iterator_data *data =
	g_malloc (sizeof (struct cz_type_iterator_data));
      data->next = 0;
      data->offset = offset;
      iter->data = data;
      *((gint *) iter->data) = 0;
      iter->next = cz_next_dentry;
      iter->free = cz_free_iterator_data;
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = -1;
      return 0;
    }
  else
    {
      return -ENOTDIR;
    }
}

static const struct fs_operations FS_PROGRAM_CZ_OPERATIONS = {
  .fs = FS_PROGRAM_CZ,
  .options =
    FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "cz",
  .gui_name = "Programs",
  .gui_icon = BE_FILE_ICON_SND,
  .readdir = cz_read_dir,
  .print_item = NULL,
  .mkdir = NULL,
  .delete = NULL,
  .rename = NULL,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = NULL,
  .upload = NULL,
  .getid = get_item_index,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = NULL,
  .get_download_path = NULL,
  .type_ext = "syx"
};

static const struct fs_operations *FS_CZ_OPERATIONS[] = {
  &FS_PROGRAM_CZ_OPERATIONS, NULL
};

static GByteArray *
cz_get_program_dump_msg (gint program)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (CZ_PROGRAM_REQUEST));
  g_byte_array_append (tx_msg, CZ_PROGRAM_REQUEST,
		       sizeof (CZ_PROGRAM_REQUEST));
  tx_msg->data[6] = (guint8) program;
  return tx_msg;
}

gint
cz_handshake (struct backend *backend)
{
  gint len;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = cz_get_program_dump_msg (0x60);	//Programmer
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = rx_msg->len;
  free_msg (rx_msg);
  if (len == CZ101_PROGRAM_LEN)
    {
      backend->device_desc.filesystems = FS_PROGRAM_CZ;
      backend->fs_ops = FS_CZ_OPERATIONS;
      backend->upgrade_os = NULL;
      snprintf (backend->device_name, LABEL_MAX, "Casio CZ-101");
      return 0;
    }
  else
    {
      return -1;
    }
}
