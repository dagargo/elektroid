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
#include "common.h"

#define CZ_PROGRAM_LEN 263
#define CZ_PROGRAM_LEN_FIXED 264
#define CZ_MAX_PROGRAMS 16
#define CZ_PRESET_PREFIX "CZ-101"
#define CZ_PROGRAM_HEADER_ID 6
#define CZ_PROGRAM_HEADER_OFFSET 6
#define CZ_MEM_TYPE_OFFSET 0x20
#define CZ_PANEL_ID 0x60
#define CZ_FIRST_CARTRIDGE_ID 0x40
#define CZ_PANEL "panel"
#define CZ_PANEL_PATH "/" CZ_PANEL

static const char *CZ_MEM_TYPES[] =
  { "preset", "internal", "cartridge", NULL };

static const guint8 CZ_PROGRAM_REQUEST[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x00, 0x70, 0x31, 0xf7 };

static const guint8 CZ_PROGRAM_HEADER[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x20, 0x00 };

enum cz_fs
{
  FS_PROGRAM_CZ
};

struct cz_type_iterator_data
{
  guint next;
  gint type;
  struct backend *backend;
};

static gchar *
cz_get_download_path (struct backend *backend,
		      const struct fs_operations *ops, const gchar *dst_dir,
		      const gchar *src_path, GByteArray *program)
{
  gchar *path;
  if (strcmp (src_path, CZ_PANEL_PATH))
    {
      gchar *filename = g_path_get_basename (src_path);
      gint id = atoi (filename);
      path = common_get_download_path_with_params (backend, ops, dst_dir, id,
						   2, NULL);
    }
  else
    {
      GString *str = g_string_new (NULL);
      g_string_append_printf (str, "%s %s panel.syx", backend->name,
			      ops->name);
      path = path_chain (PATH_SYSTEM, dst_dir, str->str);
      g_string_free (str, TRUE);
    }
  return path;
}

static GByteArray *
cz_get_program_dump_msg (guint8 id)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (CZ_PROGRAM_REQUEST));
  g_byte_array_append (tx_msg, CZ_PROGRAM_REQUEST,
		       sizeof (CZ_PROGRAM_REQUEST));
  tx_msg->data[CZ_PROGRAM_HEADER_OFFSET] = id;
  return tx_msg;
}

static gint
cz_next_dentry_root (struct item_iterator *iter)
{
  GByteArray *tx_msg, *rx_msg;
  struct cz_type_iterator_data *data = iter->data;

  if (data->next < 3)
    {
      iter->item.id = 0x1000 + data->next;	//Unique id
      snprintf (iter->item.name, LABEL_MAX, "%s", CZ_MEM_TYPES[data->next]);
      iter->item.type = ELEKTROID_DIR;
      iter->item.size = -1;

      if (data->next == 2)
	{
	  tx_msg = cz_get_program_dump_msg (CZ_FIRST_CARTRIDGE_ID);
	  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg,
					    BE_SYSEX_TIMEOUT_GUESS_MS);
	  data->next++;
	  if (rx_msg)
	    {
	      free_msg (rx_msg);
	      return 0;
	    }
	}
      else
	{
	  data->next++;
	  return 0;
	}
    }

  if (data->next == 3)
    {
      iter->item.id = CZ_PANEL_ID;
      snprintf (iter->item.name, LABEL_MAX, "panel");
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = CZ_PROGRAM_LEN_FIXED;
      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
cz_next_dentry (struct item_iterator *iter)
{
  struct cz_type_iterator_data *data = iter->data;

  if (data->next >= CZ_MAX_PROGRAMS)
    {
      return -ENOENT;
    }

  iter->item.id = data->next + data->type * CZ_MEM_TYPE_OFFSET;
  snprintf (iter->item.name, LABEL_MAX, "%d", data->next + 1);
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = CZ_PROGRAM_LEN_FIXED;
  data->next++;

  return 0;
}

static gint
get_mem_type (const gchar *name)
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
	     const gchar *path, const gchar **extensions)
{
  gint mem_type;

  if (!strcmp (path, "/"))
    {
      struct cz_type_iterator_data *data =
	g_malloc (sizeof (struct cz_type_iterator_data));
      data->next = 0;
      data->type = -1;
      data->backend = backend;
      iter->data = data;
      iter->next = cz_next_dentry_root;
      iter->free = g_free;
      return 0;
    }
  else if ((mem_type = get_mem_type (&path[1])) >= 0)
    {
      struct cz_type_iterator_data *data =
	g_malloc (sizeof (struct cz_type_iterator_data));
      data->next = 0;
      data->type = mem_type;
      data->backend = backend;
      iter->data = data;
      iter->next = cz_next_dentry;
      iter->free = g_free;
      return 0;
    }
  else
    {
      return -ENOTDIR;
    }
}

static gint
cz_download (struct backend *backend, const gchar *path,
	     GByteArray *output, struct job_control *control)
{
  guint8 id;
  gint len, type, err = 0;
  GByteArray *tx_msg, *rx_msg;
  gchar *dir, *name;

  if (strcmp (path, CZ_PANEL_PATH))
    {
      dir = g_path_get_dirname (path);
      type = get_mem_type (&dir[1]);
      g_free (dir);
      if (type < 0)
	{
	  err = -EINVAL;
	  goto end;
	}

      name = g_path_get_basename (path);
      id = atoi (name);
      g_free (name);

      id--;
      if (id >= CZ_MAX_PROGRAMS)
	{
	  err = -EINVAL;
	  goto end;
	}
      id += type * CZ_MEM_TYPE_OFFSET;
    }
  else
    {
      id = CZ_PANEL_ID;
    }

  tx_msg = cz_get_program_dump_msg (id);
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  len = rx_msg->len;
  if (len != CZ_PROGRAM_LEN)
    {
      err = -EINVAL;
      goto cleanup;
    }

  g_byte_array_append (output, CZ_PROGRAM_HEADER, sizeof (CZ_PROGRAM_HEADER));
  g_byte_array_append (output, &rx_msg->data[CZ_PROGRAM_HEADER_OFFSET],
		       CZ_PROGRAM_LEN - CZ_PROGRAM_HEADER_OFFSET);
  output->data[CZ_PROGRAM_HEADER_ID] = id;

cleanup:
  free_msg (rx_msg);
end:
  return err;
}

static gint
cz_upload (struct backend *backend, const gchar *path, GByteArray *input,
	   struct job_control *control)
{
  guint8 id;
  GByteArray *msg;
  gchar *dir, *name;
  gint type, err = 0;

  if (input->len != CZ_PROGRAM_LEN_FIXED)
    {
      return -EINVAL;
    }

  dir = g_path_get_dirname (path);
  type = get_mem_type (&dir[1]);
  g_free (dir);
  if (type >= 0)
    {
      name = g_path_get_basename (path);
      id = atoi (name);
      g_free (name);
      id--;
      if (id >= CZ_MAX_PROGRAMS)
	{
	  return -EINVAL;
	}
      id += type * CZ_MEM_TYPE_OFFSET;
    }
  else
    {
      if (strncmp (path, CZ_PANEL_PATH, strlen (CZ_PANEL_PATH)))
	{
	  return -EIO;
	}
      else
	{
	  id = CZ_PANEL_ID;
	}
    }

  msg = g_byte_array_sized_new (input->len);
  g_byte_array_append (msg, input->data, input->len);
  msg->data[CZ_PROGRAM_HEADER_ID] = id;

  err = common_data_tx (backend, msg, control);
  free_msg (msg);

  return err;
}

//As program X in preset storage and program X in internal staorage have different IDs
//it won't work with the id as the filename so the item name must be used.

static const struct fs_operations FS_PROGRAM_CZ_OPERATIONS = {
  .id = FS_PROGRAM_CZ,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SORT_BY_ID | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "program",
  .gui_name = "Programs",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .readdir = cz_read_dir,
  .print_item = common_print_item,
  .download = cz_download,
  .upload = cz_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = cz_get_download_path,
  .select_item = common_midi_program_change
};

gint
cz_handshake (struct backend *backend)
{
  gint len, err = 0;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = cz_get_program_dump_msg (CZ_PANEL_ID);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
				    BE_SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -ENODEV;
    }
  len = rx_msg->len;
  if (len != CZ_PROGRAM_LEN)
    {
      err = -ENODEV;
      goto end;
    }

  backend_fill_fs_ops (backend, &FS_PROGRAM_CZ_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "Casio CZ-101");

end:
  free_msg (rx_msg);
  return err;
}
