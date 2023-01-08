/*
 *   summit.c
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
#include "summit.h"
#include "common.h"
#include "scala.h"

#define SUMMIT_PATCHES_PER_BANK 128
#define SUMMIT_PATCH_NAME_LEN 16
#define SUMMIT_SINGLE_LEN 527
#define SUMMIT_MULTI_LEN 1039
#define SUMMIT_REST_TIME_US 5000
#define SUMMIT_MSG_BANK_POS 12
#define SUMMIT_MSG_PATCH_POS 13
#define SUMMIT_MAX_TUNINGS 17	// Tuning 0 is stored but can't be changed form the UI.

#define SUMMIT_GET_NAME_FROM_MSG(msg, type) (&msg->data[type == FS_SUMMIT_SINGLE_PATCH ? 0x10 : 0x19b])
#define SUMMIT_GET_BANK_ID_FROM_DIR(dir) ((guint8) dir[1] - 0x40)	// Bank A is the bank 1.

static const guint8 NOVATION_ID[] = { 0x0, 0x20, 0x29 };
static const guint8 SUMMIT_ID[] = { 0x33, 1, 0, 0 };

static const guint8 SUMMIT_PATCH_REQ[] =
  { 0xf0, 0, 0x20, 0x29, 0x01, 0x11, 0x01, 0x33, 0, 0, 0, 0, 0, 0, 0xf7 };

static const guint8 SUMMIT_BULK_TUNING_REQ[] =
  { 0xf0, 0x7e, 0x00, 0x08, 0x00, 0x00, 0xf7 };

enum summit_fs
{
  FS_SUMMIT_SINGLE_PATCH = 1,
  FS_SUMMIT_MULTI_PATCH = 2,
  FS_SUMMIT_SCALE = 4,
  FS_SUMMIT_BULK_TUNING = 8
};

struct summit_root_iterator_data
{
  guint next;
};

struct summit_bank_iterator_data
{
  guint next;
  enum summit_fs fs;
  gchar names[SUMMIT_PATCHES_PER_BANK][SUMMIT_PATCH_NAME_LEN + 1];
};

static gint
summit_set_patch_bank_and_id (GByteArray * msg, guint8 bank, guint8 id)
{
  if (msg->len <= SUMMIT_MSG_PATCH_POS)
    {
      return -EINVAL;
    }
  msg->data[SUMMIT_MSG_BANK_POS] = bank;
  msg->data[SUMMIT_MSG_PATCH_POS] = id;
  return 0;
}

static GByteArray *
summit_get_patch_dump_msg (gint bank, gint id, enum summit_fs fs)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SUMMIT_PATCH_REQ));
  g_byte_array_append (tx_msg, SUMMIT_PATCH_REQ, sizeof (SUMMIT_PATCH_REQ));
  tx_msg->data[8] = fs == FS_SUMMIT_SINGLE_PATCH ? 0x41 : 0x43;
  summit_set_patch_bank_and_id (tx_msg, bank, id);
  return tx_msg;
}

static void
summit_truncate_name_at_last_useful_char (gchar * c)
{
  for (int i = SUMMIT_PATCH_NAME_LEN - 1; i >= 0; i--, c--)
    {
      if (*c == ' ')
	{
	  *c = 0;
	}
      else
	{
	  break;
	}
    }
}

static gchar *
summit_get_download_path (struct backend *backend,
			  const struct fs_operations *ops,
			  const gchar * dst_dir, const gchar * src_path,
			  GByteArray * patch)
{
  guint id;
  gchar *path;
  gchar name[SUMMIT_PATCH_NAME_LEN + 1];

  if (common_slot_get_id_name_from_path (src_path, &id, NULL))
    {
      return NULL;
    }

  memcpy (name, SUMMIT_GET_NAME_FROM_MSG (patch, ops->fs),
	  SUMMIT_PATCH_NAME_LEN);
  name[SUMMIT_PATCH_NAME_LEN] = 0;
  summit_truncate_name_at_last_useful_char (&name[SUMMIT_PATCH_NAME_LEN - 1]);
  path = common_get_download_path_with_params (backend, ops, dst_dir, id, 3,
					       name);

  return path;
}

static guint
summit_next_dentry (struct item_iterator *iter)
{
  struct summit_bank_iterator_data *data = iter->data;

  if (data->next < SUMMIT_PATCHES_PER_BANK)
    {
      iter->item.id = data->next;
      snprintf (iter->item.name, LABEL_MAX, "%s", data->names[data->next]);
      iter->item.type = ELEKTROID_FILE;
      iter->item.size =
	data->fs ==
	FS_SUMMIT_SINGLE_PATCH ? SUMMIT_SINGLE_LEN : SUMMIT_MULTI_LEN;
      data->next++;
      return 0;
    }

  return -ENOENT;
}

static guint
summit_next_dentry_root (struct item_iterator *iter)
{
  struct summit_root_iterator_data *data = iter->data;

  if (data->next < 4)
    {
      iter->item.id = 0x10000 + data->next;	//Unique id
      snprintf (iter->item.name, LABEL_MAX, "%c", 0x41 + iter->item.id);
      iter->item.type = ELEKTROID_DIR;
      iter->item.size = -1;
      data->next++;
      return 0;
    }

  return -ENOENT;
}

static gint
summit_common_read_dir (struct backend *backend, struct item_iterator *iter,
			const gchar * path, enum summit_fs fs)
{
  GByteArray *tx_msg, *rx_msg;
  guint bank;

  if (!strcmp (path, "/"))
    {
      struct summit_root_iterator_data *data =
	g_malloc (sizeof (struct summit_root_iterator_data));
      data->next = 0;
      iter->data = data;
      iter->next = summit_next_dentry_root;
      iter->free = g_free;
      return 0;
    }

  bank = SUMMIT_GET_BANK_ID_FROM_DIR (path);
  if (strlen (path) == 2 && bank >= 1 && bank <= 4)
    {
      struct summit_bank_iterator_data *data =
	g_malloc (sizeof (struct summit_bank_iterator_data));
      data->next = 0;
      data->fs = fs;
      iter->data = data;
      iter->next = summit_next_dentry;
      iter->free = g_free;

      for (gint i = 0; i < SUMMIT_PATCHES_PER_BANK; i++)
	{
	  tx_msg = summit_get_patch_dump_msg (bank, i, fs);
	  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
					    BE_SYSEX_TIMEOUT_GUESS_MS);
	  if (!rx_msg)
	    {
	      return -EIO;
	    }
	  snprintf (data->names[i], SUMMIT_PATCH_NAME_LEN + 1, "%16s",
		    SUMMIT_GET_NAME_FROM_MSG (rx_msg, fs));
	  gchar *c = &data->names[i][SUMMIT_PATCH_NAME_LEN];
	  *c = 0;
	  c--;
	  summit_truncate_name_at_last_useful_char (c);
	  free_msg (rx_msg);

	  usleep (SUMMIT_REST_TIME_US);	//Without this, it will start to act erratically, including not answering to the MIDI identity request.
	}

      return 0;
    }

  return -ENOTDIR;
}

static gint
summit_single_read_dir (struct backend *backend, struct item_iterator *iter,
			const gchar * path)
{
  return summit_common_read_dir (backend, iter, path, FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar * path)
{
  return summit_common_read_dir (backend, iter, path, FS_SUMMIT_MULTI_PATCH);
}

static guint
summit_get_bank_and_id_from_path (const gchar * path, guint8 * bank,
				  guint8 * id)
{
  if (strlen (path) < 4)
    {
      return -EINVAL;
    }

  *bank = SUMMIT_GET_BANK_ID_FROM_DIR (path);
  *id = (guint8) atoi (&path[3]);

  if (*bank < 1 || *bank > 4 || *id >= SUMMIT_PATCHES_PER_BANK)
    {
      return -EBADSLT;
    }

  return 0;
}

static gint
summit_common_download (struct backend *backend, const gchar * path,
			GByteArray * output, struct job_control *control,
			enum summit_fs fs)
{
  guint8 id, bank;
  gint len, err;
  GByteArray *tx_msg, *rx_msg;

  err = summit_get_bank_and_id_from_path (path, &bank, &id);
  if (err)
    {
      goto end;
    }

  tx_msg = summit_get_patch_dump_msg (bank, id, fs);
  err = common_data_download (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  len = (fs == FS_SUMMIT_SINGLE_PATCH ? SUMMIT_SINGLE_LEN : SUMMIT_MULTI_LEN);
  if (rx_msg->len != len)
    {
      err = -EINVAL;
      goto cleanup;
    }

  g_byte_array_append (output, rx_msg->data, rx_msg->len);

cleanup:
  free_msg (rx_msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_single_download (struct backend *backend, const gchar * path,
			GByteArray * output, struct job_control *control)
{
  return summit_common_download (backend, path, output, control,
				 FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_download (struct backend *backend, const gchar * path,
		       GByteArray * output, struct job_control *control)
{
  return summit_common_download (backend, path, output, control,
				 FS_SUMMIT_MULTI_PATCH);
}

static gint
summit_common_upload (struct backend *backend, const gchar * path,
		      GByteArray * input, struct job_control *control)
{
  guint8 id, bank;
  gint err;
  GByteArray *msg;

  err = summit_get_bank_and_id_from_path (path, &bank, &id);
  if (err)
    {
      goto end;
    }

  msg = g_byte_array_sized_new (input->len);
  g_byte_array_append (msg, input->data, input->len);

  err = summit_set_patch_bank_and_id (msg, bank, id);
  if (err)
    {
      goto cleanup;
    }

  err = common_data_upload (backend, msg, control);

cleanup:
  free_msg (msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_single_upload (struct backend *backend, const gchar * path,
		      GByteArray * input, struct job_control *control)
{
  if (input->len != SUMMIT_SINGLE_LEN)
    {
      return -EINVAL;
    }

  return summit_common_upload (backend, path, input, control);
}

static gint
summit_multi_upload (struct backend *backend, const gchar * path,
		     GByteArray * input, struct job_control *control)
{
  if (input->len != SUMMIT_MULTI_LEN)
    {
      return -EINVAL;
    }

  return summit_common_upload (backend, path, input, control);
}


static gint
summit_common_rename (struct backend *backend, const gchar * src,
		      const gchar * dst, enum summit_fs fs)
{
  GByteArray *preset, *rx_msg;
  gint err, len;
  guint8 *name;
  struct job_control control;
  debug_print (1, "Renaming from %s to %s...\n", src, dst);

  //The control initialization is needed.
  control.active = TRUE;
  control.callback = NULL;
  g_mutex_init (&control.mutex);

  preset = g_byte_array_sized_new (1024);
  err = summit_common_download (backend, src, preset, &control, fs);
  if (err)
    {
      free_msg (preset);
      return err;
    }

  usleep (SUMMIT_REST_TIME_US);

  len = strlen (dst);
  name = SUMMIT_GET_NAME_FROM_MSG (preset, fs);
  memcpy (name, dst, len);
  memset (name + len, ' ', SUMMIT_PATCH_NAME_LEN - len);

  rx_msg = backend_tx_and_rx_sysex (backend, preset, 100);	//There must be no response.
  if (rx_msg)
    {
      err = -EIO;
      free_msg (rx_msg);
    }

  usleep (SUMMIT_REST_TIME_US);

  return err;
}

static gint
summit_single_rename (struct backend *backend, const gchar * src,
		      const gchar * dst)
{
  return summit_common_rename (backend, src, dst, FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_rename (struct backend *backend, const gchar * src,
		     const gchar * dst)
{
  return summit_common_rename (backend, src, dst, FS_SUMMIT_MULTI_PATCH);
}

static gchar *
summit_get_id_as_slot (struct item *item, struct backend *backend)
{
  gchar *slot = malloc (LABEL_MAX);
  if (item->id < BE_MAX_MIDI_PROGRAMS)
    {
      snprintf (slot, LABEL_MAX, "%03d", item->id);
    }
  else
    {
      slot[0] = 0;
    }
  return slot;
}

static void
summit_common_patch_change (struct backend *backend, guint8 type,
			    const gchar * dir, struct item *item)
{
  //This seems to be broken on firmware 2.1 as documented in https://forum.electra.one/t/preset-novation-summit-peak/1424/24
  guint8 bank = SUMMIT_GET_BANK_ID_FROM_DIR (dir);
  backend_tx_raw (backend, (guint8 *) "\xb0\x63\x3e\xb0\x62\x00\xb0\x06", 8);	//Single o multi
  backend_tx_raw (backend, &type, 1);	//Single o multi
  backend_tx_raw (backend, (guint8 *) "\xb0\x20", 2);	//Bank
  backend_tx_raw (backend, &bank, 1);	//Bank
  common_midi_program_change (backend, dir, item);
}

static void
summit_single_patch_change (struct backend *backend, const gchar * dir,
			    struct item *item)
{
  summit_common_patch_change (backend, 0, dir, item);
}

static void
summit_multi_patch_change (struct backend *backend, const gchar * dir,
			   struct item *item)
{
  summit_common_patch_change (backend, 1, dir, item);
}

static const struct fs_operations FS_SUMMIT_SINGLE_OPERATIONS = {
  .fs = FS_SUMMIT_SINGLE_PATCH,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "single",
  .gui_name = "Single",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .max_name_len = SUMMIT_PATCH_NAME_LEN,
  .readdir = summit_single_read_dir,
  .print_item = common_print_item,
  .rename = summit_single_rename,
  .download = summit_single_download,
  .upload = summit_single_upload,
  .get_slot = summit_get_id_as_slot,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = summit_get_download_path,
  .select_item = summit_single_patch_change
};

static const struct fs_operations FS_SUMMIT_MULTI_OPERATIONS = {
  .fs = FS_SUMMIT_MULTI_PATCH,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "multi",
  .gui_name = "Multi",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .max_name_len = SUMMIT_PATCH_NAME_LEN,
  .readdir = summit_multi_read_dir,
  .print_item = common_print_item,
  .rename = summit_multi_rename,
  .download = summit_multi_download,
  .upload = summit_multi_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = summit_get_download_path,
  .select_item = summit_multi_patch_change
};

static gint
summit_scale_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar * path)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 0;
  data->max = SUMMIT_MAX_TUNINGS;
  iter->data = data;
  iter->next = common_simple_next_dentry;
  iter->free = g_free;

  return 0;
}

static gint
summit_scale_upload (struct backend *backend, const gchar * path,
		     GByteArray * input, struct job_control *control)
{
  guint id;

  if (common_slot_get_id_name_from_path (path, &id, NULL))
    {
      return -EINVAL;
    }

  input->data[2] = 0;		//0x7f does not work with the Summit.
  input->data[5] = id;		//tuning

  return common_data_upload (backend, input, control);
}

static const struct fs_operations FS_SUMMIT_SCALE_OPERATIONS = {
  .fs = FS_SUMMIT_SCALE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "scale",
  .gui_name = "Scales",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "scl",
  .readdir = summit_scale_read_dir,
  .print_item = common_print_item,
  .upload = summit_scale_upload,
  .load = scl_get_key_based_tuning_msg_from_scala_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path
};

static gint
summit_tuning_upload (struct backend *backend, const gchar * path,
		      GByteArray * input, struct job_control *control)
{
  if (input->len != SCALA_TUNING_BANK_SIZE)
    {
      return -EINVAL;
    }
  return summit_scale_upload (backend, path, input, control);
}

static gint
summit_tuning_download (struct backend *backend, const gchar * path,
			GByteArray * output, struct job_control *control)
{
  guint32 id;
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;

  common_slot_get_id_name_from_path (path, &id, NULL);
  tx_msg = g_byte_array_sized_new (16);
  g_byte_array_append (tx_msg, SUMMIT_BULK_TUNING_REQ,
		       sizeof (SUMMIT_BULK_TUNING_REQ));

  if (id >= SUMMIT_MAX_TUNINGS)
    {
      return -EBADSLT;
    }

  tx_msg->data[5] = id;
  err = common_data_download (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  if (rx_msg->len != SCALA_TUNING_BANK_SIZE)
    {
      err = -EINVAL;
      goto cleanup;
    }

  g_byte_array_append (output, rx_msg->data, rx_msg->len);

cleanup:
  free_msg (rx_msg);
end:
  return err;
}

static const struct fs_operations FS_SUMMIT_BULK_TUNING_OPERATIONS = {
  .fs = FS_SUMMIT_BULK_TUNING,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "tuning",
  .gui_name = "Tunings",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .readdir = summit_scale_read_dir,
  .print_item = common_print_item,
  .download = summit_tuning_download,
  .upload = summit_tuning_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_download_path = common_get_download_path,
  .get_upload_path = common_slot_get_upload_path
};

static const struct fs_operations *FS_SUMMIT_OPERATIONS[] = {
  &FS_SUMMIT_SINGLE_OPERATIONS, &FS_SUMMIT_MULTI_OPERATIONS,
  &FS_SUMMIT_SCALE_OPERATIONS, &FS_SUMMIT_BULK_TUNING_OPERATIONS, NULL
};

gint
summit_handshake (struct backend *backend)
{
  gboolean novation = memcmp (backend->midi_info.company, NOVATION_ID,
			      sizeof (NOVATION_ID)) == 0;
  gboolean summit =
    memcmp (backend->midi_info.family, SUMMIT_ID, sizeof (SUMMIT_ID)) == 0;

  if (!novation || (!summit))
    {
      return -ENODEV;
    }

  backend->filesystems =
    FS_SUMMIT_SINGLE_PATCH | FS_SUMMIT_MULTI_PATCH | FS_SUMMIT_SCALE |
    FS_SUMMIT_BULK_TUNING;
  backend->fs_ops = FS_SUMMIT_OPERATIONS;
  snprintf (backend->name, LABEL_MAX, "Novation Summit");
  return 0;
}
