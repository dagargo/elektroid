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

#include <libgen.h>
#include "cz.h"

#define CZ101_PROGRAM_LEN 263
#define CZ101_PROGRAM_LEN_FIXED 264
#define CZ101_MAX_PROGRAMS 16
#define CZ101_PRESET_PREFIX "CZ-101"
#define CZ101_PROGRAM_HEADER_ID 6
#define CZ101_PROGRAM_HEADER_OFFSET 6
#define CZ101_MEM_TYPE_OFFSET 0x20
#define CZ101_PANEL_ID 0x60

static const char *CZ_MEM_TYPES[] =
  { "preset", "internal", "cartridge", NULL };

static const guint8 CZ101_PROGRAM_REQUEST[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x00, 0x70, 0x31, 0xf7 };

static const guint8 CZ101_PROGRAM_HEADER[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x20, 0x00 };

enum cz_fs
{
  FS_PROGRAM_CZ = 1
};

struct cz_type_iterator_data
{
  guint next;
  gint type;
};

static gchar *
cz_get_upload_path (struct backend *backend,
		    struct item_iterator *remote_iter,
		    const struct fs_operations *ops, const gchar * dst_dir,
		    const gchar * src_path, gint32 * next_index)
{
  //In the SLOT mode, dst_dir includes the index, ':' and the item name.
  return strdup (dst_dir);
}

static gchar *
cz_get_download_path (struct backend *backend,
		      struct item_iterator *remote_iter,
		      const struct fs_operations *ops, const gchar * dst_dir,
		      const gchar * src_path)
{
  gchar *name = malloc (PATH_MAX);
  if (strcmp (&src_path[1], "panel"))
    {
      gchar *src_path_copy = strdup (src_path);
      gchar *src_dir_copy = strdup (src_path);
      gchar *filename = basename (src_path_copy);
      gchar *dir = dirname (src_dir_copy);
      gint index = atoi (filename);
      snprintf (name, PATH_MAX, "%s/%s %s %02d.syx", dst_dir,
		CZ101_PRESET_PREFIX, &dir[1], index);
      g_free (src_path_copy);
      g_free (src_dir_copy);
    }
  else
    {
      snprintf (name, PATH_MAX, "%s/%s panel.syx", dst_dir,
		CZ101_PRESET_PREFIX);
    }
  return name;
}

static guint
cz_next_dentry_root (struct item_iterator *iter)
{
  struct cz_type_iterator_data *data = iter->data;

  if (data->next < 3)
    {
      iter->item.id = 0x1000 + data->next;	//Unique id
      snprintf (iter->item.name, LABEL_MAX, "%s", CZ_MEM_TYPES[data->next]);
      iter->item.type = ELEKTROID_DIR;
      iter->item.size = -1;
      data->next++;
      return 0;
    }
  else if (data->next == 3)
    {
      iter->item.id = CZ101_PANEL_ID;
      snprintf (iter->item.name, LABEL_MAX, "panel");
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = CZ101_PROGRAM_LEN_FIXED;
      data->next++;
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

  if (data->next >= CZ101_MAX_PROGRAMS)
    {
      return -ENOENT;
    }

  iter->item.id = data->next + data->type * CZ101_MEM_TYPE_OFFSET;
  snprintf (iter->item.name, LABEL_MAX, "%d", data->next + 1);
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = CZ101_PROGRAM_LEN_FIXED;
  data->next++;

  return 0;
}

static gint
cz_copy_iterator (struct item_iterator *dst, struct item_iterator *src,
                 gboolean cached)
{
  struct cz_type_iterator_data *data = src->data;

  dst->data = g_malloc (sizeof (struct cz_type_iterator_data));
  ((struct cz_type_iterator_data *) (dst->data))->next = 0;
  ((struct cz_type_iterator_data *) (dst->data))->type = data->type;
  dst->next = cz_next_dentry_root;
  dst->free = g_free;
  dst->copy = cz_copy_iterator;

  return 0;
}

static gint
get_mem_type (const gchar * name)
{
  const char **mem_type = CZ_MEM_TYPES;
  for (int i = 0; *mem_type; i++, mem_type++)
    {
      if (!strcmp (*mem_type, name))
	{
	  return i;
	}
    }
  return -1;
}

static gint
cz_read_dir (struct backend *backend, struct item_iterator *iter,
	     const gchar * path)
{
  gint mem_type;

  if (!strcmp (path, "/"))
    {
      struct cz_type_iterator_data *data =
	g_malloc (sizeof (struct cz_type_iterator_data));
      data->next = 0;
      data->type = -1;
      iter->data = data;
      iter->next = cz_next_dentry_root;
      iter->copy = cz_copy_iterator;
      iter->free = g_free;
      return 0;
    }
  else if ((mem_type = get_mem_type (&path[1])) >= 0)
    {
      struct cz_type_iterator_data *data =
	g_malloc (sizeof (struct cz_type_iterator_data));
      data->next = 0;
      data->type = mem_type;
      iter->data = data;
      iter->next = cz_next_dentry;
      iter->copy = cz_copy_iterator;
      iter->free = g_free;
      return 0;
    }
  else
    {
      return -ENOTDIR;
    }
}

static GByteArray *
cz_get_program_dump_msg (guint8 id)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (CZ101_PROGRAM_REQUEST));
  g_byte_array_append (tx_msg, CZ101_PROGRAM_REQUEST,
		       sizeof (CZ101_PROGRAM_REQUEST));
  tx_msg->data[CZ101_PROGRAM_HEADER_OFFSET] = id;
  return tx_msg;
}

static gint
cz_download (struct backend *backend, const gchar * path,
	     GByteArray * output, struct job_control *control)
{
  guint8 id;
  gboolean active;
  gint len, type, err = 0;
  GByteArray *tx_msg, *rx_msg;
  gchar *dirname_copy, *dir, *basename_copy;

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  if (strcmp (path, "/panel"))
    {
      dirname_copy = strdup (path);
      dir = dirname (dirname_copy);
      type = get_mem_type (&dir[1]);
      g_free (dirname_copy);
      if (type < 0)
	{
	  err = -EINVAL;
	  goto end;
	}

      basename_copy = strdup (path);
      id = atoi (basename (basename_copy)) - 1 + type * CZ101_MEM_TYPE_OFFSET;
      g_free (basename_copy);
    }
  else
    {
      id = CZ101_PANEL_ID;
    }

  tx_msg = cz_get_program_dump_msg (id);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = rx_msg->len;
  if (len != CZ101_PROGRAM_LEN)
    {
      err = -EINVAL;
      goto cleanup;
    }

  g_byte_array_append (output, CZ101_PROGRAM_HEADER,
		       sizeof (CZ101_PROGRAM_HEADER));
  g_byte_array_append (output, &rx_msg->data[CZ101_PROGRAM_HEADER_OFFSET],
		       CZ101_PROGRAM_LEN - CZ101_PROGRAM_HEADER_OFFSET);
  output->data[CZ101_PROGRAM_HEADER_ID] = id;

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  if (active)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      err = -ECANCELED;
    }

cleanup:
  free_msg (rx_msg);
end:
  return err;
}

static gint
cz_upload (struct backend *backend, const gchar * path, GByteArray * input,
	   struct job_control *control)
{
  guint8 id;
  gboolean active;
  struct sysex_transfer transfer;
  char *name_copy, *dir_copy, *dir;
  gint mem_type, err = 0;

  dir_copy = strdup (path);
  dir = dirname (dir_copy);
  mem_type = get_mem_type (&dir[1]);
  if (mem_type < 0)
    {
      err = -EIO;
      goto cleanup;
    }

  name_copy = strdup (path);
  id = atoi (basename (name_copy)) - 1 + mem_type * CZ101_MEM_TYPE_OFFSET;
  g_free (name_copy);

  g_mutex_lock (&backend->mutex);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  transfer.raw = g_byte_array_sized_new (input->len);
  g_byte_array_append (transfer.raw, input->data, input->len);
  transfer.raw->data[CZ101_PROGRAM_HEADER_ID] = id;

  transfer.active = TRUE;
  transfer.timeout = SYSEX_TIMEOUT_MS;
  err = backend_tx_sysex (backend, &transfer);
  free_msg (transfer.raw);
  if (err < 0)
    {
      goto cleanup;
    }

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  if (active)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      err = -ECANCELED;
    }

cleanup:
  g_mutex_unlock (&backend->mutex);
  g_free (dir_copy);
  return err;
}

static const struct fs_operations FS_PROGRAM_CZ_OPERATIONS = {
  .fs = FS_PROGRAM_CZ,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE,
  .name = "cz101",
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
  .download = cz_download,
  .upload = cz_upload,
  .getid = get_item_name,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = cz_get_upload_path,
  .get_download_path = cz_get_download_path,
  .type_ext = "syx"
};

static const struct fs_operations *FS_CZ_OPERATIONS[] = {
  &FS_PROGRAM_CZ_OPERATIONS, NULL
};

gint
cz_handshake (struct backend *backend)
{
  gint len, err = 0;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = cz_get_program_dump_msg (0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = rx_msg->len;
  if (len != CZ101_PROGRAM_LEN)
    {
      err = -EIO;
      goto end;
    }

  backend->device_desc.filesystems = FS_PROGRAM_CZ;
  backend->fs_ops = FS_CZ_OPERATIONS;
  backend->upgrade_os = NULL;
  snprintf (backend->device_name, LABEL_MAX, "Casio CZ-101");

end:
  free_msg (rx_msg);
  return err;
}
