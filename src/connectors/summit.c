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

#include "summit.h"
#include "common.h"
#include "scala.h"

#define SUMMIT_PATCHES_PER_BANK 128
#define SUMMIT_PATCH_NAME_LEN 16
#define SUMMIT_SINGLE_LEN 527
#define SUMMIT_MULTI_LEN 1039
#define SUMMIT_REST_TIME_US 10000
#define SUMMIT_MSG_BANK_POS 12
#define SUMMIT_MSG_PATCH_POS 13
#define SUMMIT_MAX_TUNINGS 17	// Tuning 0 is stored but can't be changed from the UI.
#define SUMMIT_MAX_WAVETABLES 10
#define SUMMIT_WAVETABLE_NAME_LEN 7	//In the device there are 8 available characters in the wavetable names but the wavetable messages only contain the first 7 characters.
#define SUMMIT_WAVETABLE_HEADER_LEN 23
#define SUMMIT_WAVETABLE_WAVE_LEN 531
#define SUMMIT_WAVETABLE_WAVES 5
#define SUMMIT_WAVETABLE_LEN (SUMMIT_WAVETABLE_HEADER_LEN + SUMMIT_WAVETABLE_WAVES * SUMMIT_WAVETABLE_WAVE_LEN)
#define SUMMIT_WAVETABLE_ID_POS 14
#define SUMMIT_REQ_OP_POS 8

#define SUMMIT_GET_NAME_FROM_MSG(msg, type) (&msg->data[type == FS_SUMMIT_SINGLE_PATCH ? 0x10 : 0x19b])
#define SUMMIT_GET_BANK_ID_FROM_DIR(dir) ((guint8) dir[1] - 0x40)	// Bank A is the bank 1.

#define SUMMIT_ALPHABET " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}"
#define SUMMIT_DEFAULT_CHAR '?'

static const gchar *SUMMIT_TUNING_EXTS[] = { BE_SYSEX_EXT, SCALA_EXT, NULL };

static const guint8 NOVATION_ID[] = { 0x0, 0x20, 0x29 };
static const guint8 SUMMIT_ID[] = { 0x33, 1, 0, 0 };

static const guint8 SUMMIT_GENERIC_REQ[] =
  { 0xf0, 0, 0x20, 0x29, 0x01, 0x11, 0x01, 0x33, 0, 0, 0, 0, 0, 0, 0xf7 };

static const guint8 SUMMIT_BULK_TUNING_REQ[] =
  { 0xf0, 0x7e, 0x00, 0x08, 0x00, 0x00, 0xf7 };

enum summit_fs
{
  FS_SUMMIT_SINGLE_PATCH,
  FS_SUMMIT_MULTI_PATCH,
  FS_SUMMIT_WAVETABLE,
  FS_SUMMIT_BULK_TUNING
};

struct summit_bank_iterator_data
{
  guint8 next;
  guint8 bank;
  enum summit_fs fs;
  struct backend *backend;
};

struct summit_wavetable_iterator_data
{
  guint next;
  struct backend *backend;
};

static gint
summit_set_patch_bank_and_id (GByteArray *msg, guint8 bank, guint8 id)
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
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SUMMIT_GENERIC_REQ));
  g_byte_array_append (tx_msg, SUMMIT_GENERIC_REQ,
		       sizeof (SUMMIT_GENERIC_REQ));
  tx_msg->data[SUMMIT_REQ_OP_POS] =
    fs == FS_SUMMIT_SINGLE_PATCH ? 0x41 : 0x43;
  summit_set_patch_bank_and_id (tx_msg, bank, id);
  return tx_msg;
}

//This function truncates the name to the last useful char ignoring the trailing spaces.

static void
summit_truncate_name (gchar *c)
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

static const gchar *
summit_get_category_name (GByteArray *rx_msg)
{
  switch (rx_msg->data[32])
    {
    case 0:
      return "None";
    case 1:
      return "Arp";
    case 2:
      return "Bass";
    case 3:
      return "Bell";
    case 4:
      return "Classic";
    case 5:
      return "DrumPerc";
    case 6:
      return "Keyboard";
    case 7:
      return "Lead";
    case 8:
      return "Motion";
    case 9:
      return "Pad";
    case 10:
      return "Poly";
    case 11:
      return "SFX";
    case 12:
      return "String";
    case 13:
      return "User 1";
    case 14:
      return "User 2";
    default:
      return "";
    }
}

static gint
summit_patch_next_dentry (struct item_iterator *iter)
{
  GByteArray *tx_msg, *rx_msg;
  struct summit_bank_iterator_data *data = iter->data;

  if (data->next >= SUMMIT_PATCHES_PER_BANK)
    {
      return -ENOENT;
    }

  tx_msg = summit_get_patch_dump_msg (data->bank, data->next, data->fs);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  memcpy (iter->item.name, SUMMIT_GET_NAME_FROM_MSG (rx_msg, data->fs),
	  SUMMIT_PATCH_NAME_LEN);
  iter->item.name[SUMMIT_PATCH_NAME_LEN] = 0;
  gchar *c = &iter->item.name[SUMMIT_PATCH_NAME_LEN - 1];
  summit_truncate_name (c);
  if (data->fs == FS_SUMMIT_SINGLE_PATCH)
    {
      const gchar *category = summit_get_category_name (rx_msg);
      snprintf (iter->item.object_info, LABEL_MAX, "%s", category);
    }
  free_msg (rx_msg);

  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size =
    data->fs == FS_SUMMIT_SINGLE_PATCH ? SUMMIT_SINGLE_LEN : SUMMIT_MULTI_LEN;
  data->next++;

  usleep (SUMMIT_REST_TIME_US);

  return 0;
}

static gint
summit_patch_next_dentry_root (struct item_iterator *iter)
{
  guint *next = iter->data;

  if (*next < 4)
    {
      iter->item.id = 0x10000 + *next;	//Unique id
      snprintf (iter->item.name, LABEL_MAX, "%c", 0x41 + iter->item.id);
      iter->item.type = ITEM_TYPE_DIR;
      iter->item.size = -1;
      iter->item.object_info[0] = 0;
      (*next)++;
      return 0;
    }

  return -ENOENT;
}

static gint
summit_patch_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar *dir, enum summit_fs fs)
{
  guint bank;

  if (!strcmp (dir, "/"))
    {
      guint *next = g_malloc (sizeof (guint));
      *next = 0;
      item_iterator_init (iter, dir, next, summit_patch_next_dentry_root,
			  g_free);
      return 0;
    }

  bank = SUMMIT_GET_BANK_ID_FROM_DIR (dir);
  if (strlen (dir) == 2 && bank >= 1 && bank <= 4)
    {
      struct summit_bank_iterator_data *data =
	g_malloc (sizeof (struct summit_bank_iterator_data));
      data->next = 0;
      data->fs = fs;
      data->bank = bank;
      data->backend = backend;
      item_iterator_init (iter, dir, data, summit_patch_next_dentry, g_free);
      return 0;
    }

  return -ENOTDIR;
}

static gint
summit_single_read_dir (struct backend *backend, struct item_iterator *iter,
			const gchar *dir, const gchar **extensions)
{
  return summit_patch_read_dir (backend, iter, dir, FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar *path, const gchar **extensions)
{
  return summit_patch_read_dir (backend, iter, path, FS_SUMMIT_MULTI_PATCH);
}

static guint
summit_get_bank_and_id_from_path (const gchar *path, guint8 *bank, guint8 *id)
{
  if (strlen (path) < 4)
    {
      return -EINVAL;
    }

  *bank = SUMMIT_GET_BANK_ID_FROM_DIR (path);
  *id = (guint8) atoi (&path[3]);

  if (*bank < 1 || *bank > 4 || *id >= SUMMIT_PATCHES_PER_BANK)
    {
      return -EINVAL;
    }

  return 0;
}

static gint
summit_patch_download (struct backend *backend, const gchar *path,
		       struct idata *patch, struct task_control *control,
		       enum summit_fs fs)
{
  guint8 id, bank;
  gint len, err;
  GByteArray *tx_msg, *rx_msg;
  gchar name[SUMMIT_PATCH_NAME_LEN + 1];

  err = summit_get_bank_and_id_from_path (path, &bank, &id);
  if (err)
    {
      goto end;
    }

  tx_msg = summit_get_patch_dump_msg (bank, id, fs);
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
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

  memcpy (name, SUMMIT_GET_NAME_FROM_MSG (rx_msg, fs), SUMMIT_PATCH_NAME_LEN);
  name[SUMMIT_PATCH_NAME_LEN] = 0;
  summit_truncate_name (&name[SUMMIT_PATCH_NAME_LEN - 1]);

  idata_init (patch, rx_msg, strdup (name), NULL);
  goto end;

cleanup:
  free_msg (rx_msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_single_download (struct backend *backend, const gchar *path,
			struct idata *patch, struct task_control *control)
{
  return summit_patch_download (backend, path, patch, control,
				FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_download (struct backend *backend, const gchar *path,
		       struct idata *patch, struct task_control *control)
{
  return summit_patch_download (backend, path, patch, control,
				FS_SUMMIT_MULTI_PATCH);
}

static gint
summit_patch_upload (struct backend *backend, const gchar *path,
		     GByteArray *input, struct task_control *control)
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

  err = common_data_tx (backend, msg, control);

cleanup:
  free_msg (msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_single_upload (struct backend *backend, const gchar *path,
		      struct idata *patch, struct task_control *control)
{
  if (patch->content->len != SUMMIT_SINGLE_LEN)
    {
      return -EINVAL;
    }

  return summit_patch_upload (backend, path, patch->content, control);
}

static gint
summit_multi_upload (struct backend *backend, const gchar *path,
		     struct idata *patch, struct task_control *control)
{
  if (patch->content->len != SUMMIT_MULTI_LEN)
    {
      return -EINVAL;
    }

  return summit_patch_upload (backend, path, patch->content, control);
}

static gint
summit_patch_rename (struct backend *backend, const gchar *src,
		     const gchar *dst, enum summit_fs fs)
{
  struct idata preset;
  GByteArray *rx_msg;
  gint err, len;
  guint8 *name;
  gchar *sanitized;
  struct task_control control;
  debug_print (1, "Renaming from %s to %s...", src, dst);

  controllable_init (&control.controllable);
  control.callback = NULL;

  err = summit_patch_download (backend, src, &preset, &control, fs);
  if (err)
    {
      goto end;
    }

  usleep (SUMMIT_REST_TIME_US);

  name = SUMMIT_GET_NAME_FROM_MSG (preset.content, fs);
  sanitized = common_get_sanitized_name (dst, SUMMIT_ALPHABET,
					 SUMMIT_DEFAULT_CHAR);
  len = strlen (sanitized);
  len = len > SUMMIT_PATCH_NAME_LEN ? SUMMIT_PATCH_NAME_LEN : len;
  memcpy (name, sanitized, len);
  g_free (sanitized);
  memset (name + len, ' ', SUMMIT_PATCH_NAME_LEN - len);

  rx_msg = backend_tx_and_rx_sysex (backend, preset.content, 100);	//There must be no response.
  if (rx_msg)
    {
      err = -EIO;
      free_msg (rx_msg);
    }

  usleep (SUMMIT_REST_TIME_US);

end:
  controllable_clear (&control.controllable);
  return err;
}

static gint
summit_single_rename (struct backend *backend, const gchar *src,
		      const gchar *dst)
{
  return summit_patch_rename (backend, src, dst, FS_SUMMIT_SINGLE_PATCH);
}

static gint
summit_multi_rename (struct backend *backend, const gchar *src,
		     const gchar *dst)
{
  return summit_patch_rename (backend, src, dst, FS_SUMMIT_MULTI_PATCH);
}

static gchar *
summit_get_id_as_slot (struct item *item, struct backend *backend,
		       gint digits)
{
  gchar *slot = g_malloc (LABEL_MAX);
  if (item->id < BE_MAX_MIDI_PROGRAMS)
    {
      snprintf (slot, LABEL_MAX, "%.*d", digits, item->id);
    }
  else
    {
      slot[0] = 0;
    }
  return slot;
}

static gchar *
summit_get_patch_id_as_slot (struct item *item, struct backend *backend)
{
  return summit_get_id_as_slot (item, backend, 3);
}

static void
summit_common_patch_change (struct backend *backend, guint8 type,
			    const gchar *dir, struct item *item)
{
  guint8 msg[3];

  if (!strcmp (dir, "/"))
    {
      return;
    }

  //This seems to be broken on firmware 2.1 as documented in https://forum.electra.one/t/preset-novation-summit-peak/1424/24
  //Single o multi
  backend_tx_raw (backend, (guint8 *) "\xb0\x63\x3e", 3);
  backend_tx_raw (backend, (guint8 *) "\xb0\x62\x00", 3);
  memcpy (msg, "\xb0\x06", 2);
  msg[2] = type;
  backend_tx_raw (backend, msg, 3);
  //Bank
  memcpy (msg, "\xb0\x20", 2);
  msg[2] = SUMMIT_GET_BANK_ID_FROM_DIR (dir);
  backend_tx_raw (backend, msg, 3);
  //Patch
  common_midi_program_change (backend, dir, item);
}

static void
summit_single_patch_change (struct backend *backend, const gchar *dir,
			    struct item *item)
{
  summit_common_patch_change (backend, 0, dir, item);
}

static void
summit_multi_patch_change (struct backend *backend, const gchar *dir,
			   struct item *item)
{
  summit_common_patch_change (backend, 1, dir, item);
}

static const struct fs_operations FS_SUMMIT_SINGLE_OPERATIONS = {
  .id = FS_SUMMIT_SINGLE_PATCH,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_SHOW_INFO_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "single",
  .gui_name = "Single",
  .gui_icon = FS_ICON_SND,
  .max_name_len = SUMMIT_PATCH_NAME_LEN,
  .readdir = summit_single_read_dir,
  .print_item = common_print_item,
  .rename = summit_single_rename,
  .download = summit_single_download,
  .upload = summit_single_upload,
  .get_slot = summit_get_patch_id_as_slot,
  .load = file_load,
  .save = file_save,
  .get_exts = common_sysex_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nnn,
  .select_item = summit_single_patch_change
};

static const struct fs_operations FS_SUMMIT_MULTI_OPERATIONS = {
  .id = FS_SUMMIT_MULTI_PATCH,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_ALLOW_SEARCH,
  .name = "multi",
  .gui_name = "Multi",
  .gui_icon = FS_ICON_SND,
  .max_name_len = SUMMIT_PATCH_NAME_LEN,
  .readdir = summit_multi_read_dir,
  .print_item = common_print_item,
  .rename = summit_multi_rename,
  .download = summit_multi_download,
  .upload = summit_multi_upload,
  .get_slot = summit_get_patch_id_as_slot,
  .load = file_load,
  .save = file_save,
  .get_exts = common_sysex_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nnn,
  .select_item = summit_multi_patch_change
};

static gint
summit_tuning_read_dir (struct backend *backend, struct item_iterator *iter,
			const gchar *dir, const gchar **extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 0;
  data->last = SUMMIT_MAX_TUNINGS - 1;

  item_iterator_init (iter, dir, data, common_simple_next_dentry, g_free);

  return 0;
}

static gint
summit_tuning_upload (struct backend *backend, const gchar *path,
		      struct idata *tuning, struct task_control *control)
{
  guint id;
  GByteArray *input = tuning->content;

  if (common_slot_get_id_from_path (path, &id))
    {
      return -EINVAL;
    }
  if (id >= SUMMIT_MAX_TUNINGS)
    {
      return -EINVAL;
    }

  if (input->len != SCALA_TUNING_BANK_SIZE)
    {
      return -EINVAL;
    }

  input->data[2] = 0;		//0x7f does not work with the Summit.
  input->data[5] = id;		//tuning

  return common_data_tx (backend, input, control);
}

static gint
summit_tuning_download (struct backend *backend, const gchar *path,
			struct idata *tuning, struct task_control *control)
{
  guint32 id;
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;

  if (common_slot_get_id_from_path (path, &id))
    {
      return -EINVAL;
    }
  if (id >= SUMMIT_MAX_TUNINGS)
    {
      return -EINVAL;
    }

  tx_msg = g_byte_array_sized_new (16);
  g_byte_array_append (tx_msg, SUMMIT_BULK_TUNING_REQ,
		       sizeof (SUMMIT_BULK_TUNING_REQ));
  tx_msg->data[5] = id;
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  if (rx_msg->len != SCALA_TUNING_BANK_SIZE)
    {
      err = -EINVAL;
      goto cleanup;
    }

  idata_init (tuning, rx_msg, NULL, NULL);
  goto end;

cleanup:
  free_msg (rx_msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_tuning_load (const gchar *path, struct idata *tuning,
		    struct task_control *control)
{
  gint err;
  gchar *filename = g_path_get_basename (path);
  if (strcmp (filename_get_ext (filename), SCALA_EXT))
    {
      err = file_load (path, tuning, control);
    }
  else
    {
      err = scl_load_key_based_tuning_msg_from_scala_file (path, tuning,
							   control);
    }
  g_free (filename);
  return err;
}

static const gchar **
summit_tuning_get_extensions (struct backend *backend,
			      const struct fs_operations *ops)
{
  return SUMMIT_TUNING_EXTS;
}

static const struct fs_operations FS_SUMMIT_BULK_TUNING_OPERATIONS = {
  .id = FS_SUMMIT_BULK_TUNING,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE,
  .name = "tuning",
  .gui_name = "Tuning Tables",
  .gui_icon = FS_ICON_KEYS,
  .readdir = summit_tuning_read_dir,
  .print_item = common_print_item,
  .download = summit_tuning_download,
  .upload = summit_tuning_upload,
  .load = summit_tuning_load,
  .save = file_save,
  .get_exts = summit_tuning_get_extensions,
  .get_download_path = common_slot_get_download_path_nn,
  .get_upload_path = common_slot_get_upload_path
};

static GByteArray *
summit_get_wavetable_header_dump_msg (guint8 id)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SUMMIT_GENERIC_REQ));
  g_byte_array_append (tx_msg, SUMMIT_GENERIC_REQ,
		       sizeof (SUMMIT_GENERIC_REQ));
  tx_msg->data[SUMMIT_REQ_OP_POS] = 0x47;
  tx_msg->data[11] = id;
  return tx_msg;
}

static GByteArray *
summit_get_wavetable_wave_dump_msg (guint id, guint8 wave)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SUMMIT_GENERIC_REQ));
  g_byte_array_append (tx_msg, SUMMIT_GENERIC_REQ,
		       sizeof (SUMMIT_GENERIC_REQ));
  tx_msg->data[SUMMIT_REQ_OP_POS] = 0x46;
  tx_msg->data[11] = id;
  tx_msg->data[12] = wave;
  return tx_msg;
}

static gint
summit_wavetable_next_dentry (struct item_iterator *iter)
{
  GByteArray *tx_msg, *rx_msg;
  struct summit_bank_iterator_data *data = iter->data;

  if (data->next >= SUMMIT_MAX_WAVETABLES)
    {
      return -ENOENT;
    }

  tx_msg = summit_get_wavetable_header_dump_msg (data->next + 64);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  memcpy (iter->item.name, &rx_msg->data[15], SUMMIT_WAVETABLE_NAME_LEN);
  iter->item.name[SUMMIT_WAVETABLE_NAME_LEN] = 0;
  gchar *c = &iter->item.name[SUMMIT_WAVETABLE_NAME_LEN - 1];
  summit_truncate_name (c);
  free_msg (rx_msg);

  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = 2678;
  data->next++;

  usleep (SUMMIT_REST_TIME_US * 10);

  return 0;
}

static gint
summit_wavetable_read_dir (struct backend *backend,
			   struct item_iterator *iter, const gchar *dir,
			   const gchar **extensions)
{
  if (!strcmp (dir, "/"))
    {
      struct summit_wavetable_iterator_data *data =
	g_malloc (sizeof (struct summit_wavetable_iterator_data));
      data->next = 0;
      data->backend = backend;
      item_iterator_init (iter, dir, data, summit_wavetable_next_dentry,
			  g_free);
      return 0;
    }

  return -ENOTDIR;
}

static gint
summit_wavetable_download (struct backend *backend, const gchar *path,
			   struct idata *wavetable,
			   struct task_control *control)
{
  guint32 id;
  gint err = 0;
  gchar name[SUMMIT_PATCH_NAME_LEN + 1];
  GByteArray *tx_msg, *rx_msg, *output;

  if (common_slot_get_id_from_path (path, &id))
    {
      return -EINVAL;
    }
  if (id >= SUMMIT_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  task_control_reset (control, 6);

  output = g_byte_array_new ();

  //Header
  tx_msg = summit_get_wavetable_header_dump_msg (id + 64);
  err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (rx_msg->len != SUMMIT_WAVETABLE_HEADER_LEN)
    {
      err = -EINVAL;
      goto err;
    }

  rx_msg->data[SUMMIT_WAVETABLE_ID_POS] = id;
  g_byte_array_append (output, rx_msg->data, rx_msg->len);
  free_msg (rx_msg);

  usleep (SUMMIT_REST_TIME_US);

  //Waves
  for (gint8 i = 0; i < SUMMIT_WAVETABLE_WAVES; i++)
    {
      tx_msg = summit_get_wavetable_wave_dump_msg (id, i);
      err = common_data_tx_and_rx_part (backend, tx_msg, &rx_msg, control);
      if (err)
	{
	  goto err;
	}
      if (rx_msg->len != SUMMIT_WAVETABLE_WAVE_LEN)
	{
	  err = -EINVAL;
	  goto err;
	}

      g_byte_array_append (output, rx_msg->data, rx_msg->len);
      free_msg (rx_msg);

      usleep (SUMMIT_REST_TIME_US);
    }

  memcpy (name, &output->data[15], SUMMIT_WAVETABLE_NAME_LEN);
  name[SUMMIT_WAVETABLE_NAME_LEN] = 0;
  summit_truncate_name (&name[SUMMIT_WAVETABLE_NAME_LEN - 1]);

  idata_init (wavetable, output, strdup (name), NULL);
  goto end;

err:
  g_byte_array_free (output, TRUE);
  free_msg (rx_msg);
end:
  usleep (SUMMIT_REST_TIME_US);
  return err;
}

static gint
summit_wavetable_upload (struct backend *backend, const gchar *path,
			 struct idata *wavetable,
			 struct task_control *control)
{
  guint id;
  GByteArray *input = wavetable->content;

  if (common_slot_get_id_from_path (path, &id))
    {
      return -EINVAL;
    }
  if (id >= SUMMIT_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  if (input->len != SUMMIT_WAVETABLE_LEN)
    {
      return -EINVAL;
    }

  //Header
  input->data[SUMMIT_WAVETABLE_ID_POS] = id;

  //Waves
  for (gint8 i = 0; i < SUMMIT_WAVETABLE_WAVES; i++)
    {
      input->data[SUMMIT_WAVETABLE_HEADER_LEN + i * SUMMIT_WAVETABLE_WAVE_LEN
		  + SUMMIT_WAVETABLE_ID_POS] = id;
    }

  return common_data_tx (backend, input, control);
}

static gint
summit_wavetable_rename (struct backend *backend, const gchar *src,
			 const gchar *dst)
{
  GByteArray *tx_msg;
  guint id, len;
  gchar *sanitized;

  if (common_slot_get_id_from_path (src, &id))
    {
      return -EINVAL;
    }
  if (id >= SUMMIT_MAX_WAVETABLES)
    {
      return -EINVAL;
    }

  debug_print (1, "Renaming from %s to %s...", src, dst);

  tx_msg = g_byte_array_sized_new (23);
  g_byte_array_append (tx_msg, SUMMIT_GENERIC_REQ,
		       sizeof (SUMMIT_GENERIC_REQ));
  tx_msg->data[SUMMIT_REQ_OP_POS] = 0x7;
  tx_msg->data[14] = id;
  g_byte_array_append (tx_msg, (guint8 *) "       \xf7", 8);
  sanitized = common_get_sanitized_name (dst, SUMMIT_ALPHABET,
					 SUMMIT_DEFAULT_CHAR);
  len = strlen (sanitized);
  len = len > SUMMIT_WAVETABLE_NAME_LEN ? SUMMIT_WAVETABLE_NAME_LEN : len;
  memcpy (&tx_msg->data[15], sanitized, len);
  g_free (sanitized);
  return backend_tx (backend, tx_msg);
}

static const struct fs_operations FS_SUMMIT_WAVETABLE_OPERATIONS = {
  .id = FS_SUMMIT_WAVETABLE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_ID_COLUMN | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "wavetable",
  .gui_name = "Wavetables",
  .gui_icon = FS_ICON_WAVETABLE,
  .max_name_len = SUMMIT_WAVETABLE_NAME_LEN,
  .readdir = summit_wavetable_read_dir,
  .print_item = common_print_item,
  .rename = summit_wavetable_rename,
  .download = summit_wavetable_download,
  .upload = summit_wavetable_upload,
  .load = file_load,
  .save = file_save,
  .get_exts = common_sysex_get_extensions,
  .get_download_path = common_slot_get_download_path_n,
  .get_upload_path = common_slot_get_upload_path
};

static gint
summit_handshake (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, NOVATION_ID, sizeof (NOVATION_ID))
      || memcmp (backend->midi_info.family, SUMMIT_ID, sizeof (SUMMIT_ID)))
    {
      return -ENODEV;
    }

  gslist_fill (&backend->fs_ops, &FS_SUMMIT_SINGLE_OPERATIONS,
	       &FS_SUMMIT_MULTI_OPERATIONS,
	       &FS_SUMMIT_WAVETABLE_OPERATIONS,
	       &FS_SUMMIT_BULK_TUNING_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "Novation Summit");

  return 0;
}

const struct connector CONNECTOR_SUMMIT = {
  .handshake = summit_handshake,
  .name = "summit",
  .options = 0,
  .regex = ".*(Peak|Summit).*"
};
