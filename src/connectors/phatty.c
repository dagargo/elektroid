/*
 *   phatty.c
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

#include "phatty.h"
#include "common.h"
#include "scala.h"

#define PHATTY_ALPHABET " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz!#$%&()*?@"

#define PHATTY_MAX_PRESETS 100
#define PHATTY_PROGRAM_SIZE 193

#define PHATTY_PRESET_ID_OFFSET 5

#define MOOG_NAME_LEN 13

#define PHATTY_PRESETS_DIR "/presets"
#define PHATTY_PANEL "panel"
#define PHATTY_PANEL_PATH "/" PHATTY_PANEL
#define PHATTY_PANEL_ID 0x100
#define PHATTY_MAX_SCALES 32

static const guint8 MOOG_ID[] = { 0x04 };
static const guint8 FAMILY_ID[] = { 0x0, 0x5 };
static const guint8 MODEL_ID[] = { 0x0, 0x1 };

static const guint8 PHATTY_REQUEST_PANEL[] =
  { 0xf0, 4, 5, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };
static const guint8 PHATTY_REQUEST_PRESET[] =
  { 0xf0, 4, 5, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };

struct phatty_iter_data
{
  guint next;
  struct backend *backend;
};

enum phatty_fs
{
  FS_PHATTY_PRESET,
  FS_PHATTY_SCALE
};

static gchar
phatty_decode_char (guint8 *data, gint position)
{
  gint index;
  gint k = (3 * (position / 2)) + 23;
  if (position % 2 == 0)
    {
      index = ((data[k] & 0x1) << 6) | (data[k + 1] & 0x3f);
    }
  else
    {
      index = ((data[k + 2] & 0x3) << 4) | ((data[k + 3] & 0x3c) >> 2);
    }
  if (index >= strlen (PHATTY_ALPHABET))
    {
      return '?';
    }
  else
    {
      return PHATTY_ALPHABET[index];
    }
}

static void
phatty_encode_char (guint8 *data, gchar c, gint position)
{
  gchar *s = PHATTY_ALPHABET;
  gint k, index = 0;
  while (*s != 0 && *s != c)
    {
      s++;
      index++;
    }
  if (!*s)
    {
      index = 0;
    }
  // Code adapted from https://gitlab.com/jp-ma/phatty-editor/blob/master/libphatty/phatty-fmt.x
  k = (3 * (position / 2)) + 23;
  if (position % 2 == 0)
    {
      data[k] &= ~0x1;
      data[k] |= (index >> 6) & 0x01;
      data[k + 1] &= ~0x3f;
      data[k + 1] |= index & 0x3f;
    }
  else
    {
      data[k + 2] &= ~0x3;
      data[k + 2] |= (index >> 4) & 0x7;
      data[k + 3] &= ~0x3c;
      data[k + 3] |= (index & 0xf) << 2;
    }
}

void
phatty_set_preset_name (guint8 *preset, const gchar *preset_name)
{
  gint i;
  const gchar *c = preset_name;
  for (i = 0; i < strlen (preset_name); i++, c++)
    {
      phatty_encode_char (preset, *c, i);
    }
  for (; i < MOOG_NAME_LEN; i++, c++)
    {
      phatty_encode_char (preset, ' ', i);
    }
}

void
phatty_get_preset_name (guint8 *preset, gchar *preset_name)
{
  gchar *c = preset_name;
  for (gint i = 0; i < MOOG_NAME_LEN; i++, c++)
    {
      *c = phatty_decode_char (preset, i);
    }
  *c = 0;
  c--;
  for (gint i = MOOG_NAME_LEN; i > 0; i--, c--)
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
phatty_get_download_path (struct backend *backend,
			  const struct fs_operations *ops,
			  const gchar *dst_dir, const gchar *src_path,
			  struct idata *preset)
{
  guint id;
  guint digits;

  if (common_slot_get_id_name_from_path (src_path, &id, NULL))
    {
      return NULL;
    }

  digits = id == PHATTY_PANEL_ID ? 0 : 2;
  return common_slot_get_download_path (backend, ops, dst_dir, src_path,
					preset, digits);
}

static GByteArray *
phatty_get_panel_dump_msg ()
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (PHATTY_REQUEST_PANEL));
  g_byte_array_append (tx_msg, PHATTY_REQUEST_PANEL,
		       sizeof (PHATTY_REQUEST_PANEL));
  return tx_msg;
}

static GByteArray *
phatty_get_preset_dump_msg (guint8 id)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (PHATTY_REQUEST_PRESET));
  g_byte_array_append (tx_msg, PHATTY_REQUEST_PRESET,
		       sizeof (PHATTY_REQUEST_PRESET));
  tx_msg->data[PHATTY_PRESET_ID_OFFSET] = id;
  return tx_msg;
}

static gint
phatty_next_root_dentry (struct item_iterator *iter)
{
  guint *next = iter->data;
  if (*next == 0)
    {
      snprintf (iter->item.name, LABEL_MAX, "%s", "presets");
      iter->item.id = 0x1000;
      iter->item.type = ITEM_TYPE_DIR;
      iter->item.size = -1;
    }
  else if (*next == 1)
    {
      snprintf (iter->item.name, LABEL_MAX, "%s", "panel");
      iter->item.id = PHATTY_PANEL_ID;
      iter->item.type = ITEM_TYPE_FILE;
      iter->item.size = -1;
    }
  else
    {
      return -ENOENT;
    }

  (*next)++;

  return 0;
}

static gint
phatty_next_preset_dentry (struct item_iterator *iter)
{
  gchar preset_name[MOOG_NAME_LEN + 1];
  GByteArray *tx_msg, *rx_msg;
  struct phatty_iter_data *data = iter->data;

  if (data->next >= PHATTY_MAX_PRESETS)
    {
      return -ENOENT;
    }

  tx_msg = phatty_get_preset_dump_msg (data->next);
  rx_msg = backend_tx_and_rx_sysex (data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  phatty_get_preset_name (rx_msg->data, preset_name);
  snprintf (iter->item.name, LABEL_MAX, "%s", preset_name);
  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = PHATTY_PROGRAM_SIZE;
  (data->next)++;

  free_msg (rx_msg);
  return 0;
}

static gint
phatty_read_dir (struct backend *backend, struct item_iterator *iter,
		 const gchar *dir, GSList *exts)
{
  gint err = 0;

  if (!strcmp (dir, "/"))
    {
      guint *id = g_malloc (sizeof (guint));
      *id = 0;
      item_iterator_init (iter, dir, id, phatty_next_root_dentry, g_free);
    }
  else if (!strcmp (dir, PHATTY_PRESETS_DIR))
    {
      struct phatty_iter_data *data =
	g_malloc (sizeof (struct phatty_iter_data));
      data->next = 0;
      data->backend = backend;
      item_iterator_init (iter, dir, data, phatty_next_preset_dentry, g_free);
    }
  else
    {
      err = -ENOTDIR;
    }

  return err;
}

gchar *
phatty_get_id_as_slot (struct item *item, struct backend *backend)
{
  gchar *slot = g_malloc (LABEL_MAX);
  if (item->id >= PHATTY_MAX_PRESETS)
    {
      slot[0] = 0;
    }
  else
    {
      snprintf (slot, LABEL_MAX, "%.2d", item->id);
    }
  return slot;
}

static gint
phatty_download (struct backend *backend, const gchar *path,
		 struct idata *preset, struct job_control *control)
{
  guint8 id;
  gint err = 0;
  gboolean panel;
  GByteArray *tx_msg, *rx_msg;
  gchar name[MOOG_NAME_LEN + 1], *basename;

  if (strcmp (path, PHATTY_PANEL_PATH))
    {
      basename = g_path_get_basename (path);
      id = atoi (basename);
      g_free (basename);
      if (id >= PHATTY_MAX_PRESETS)
	{
	  return -EINVAL;
	}
      tx_msg = phatty_get_preset_dump_msg (id);
      panel = FALSE;
    }
  else
    {
      tx_msg = phatty_get_panel_dump_msg ();
      panel = TRUE;
    }

  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  if (rx_msg->len != PHATTY_PROGRAM_SIZE)
    {
      err = -EINVAL;
      goto cleanup;
    }

  if (!panel)
    {
      phatty_get_preset_name (rx_msg->data, name);
    }
  idata_init (preset, rx_msg, strdup (panel ? PHATTY_PANEL : name), NULL);
  return 0;

cleanup:
  free_msg (rx_msg);
end:
  return err;
}

static gint
phatty_upload (struct backend *backend, const gchar *path,
	       struct idata *preset, struct job_control *control)
{
  guint id;

  if (preset->content->len != PHATTY_PROGRAM_SIZE)
    {
      return -EINVAL;
    }

  if (common_slot_get_id_name_from_path (path, &id, NULL))
    {
      return -EINVAL;
    }

  if (id >= PHATTY_MAX_PRESETS && id != PHATTY_PANEL_ID)
    {
      return -EINVAL;
    }

  if (!strcmp (path, PHATTY_PANEL_PATH))
    {
      return -EINVAL;
    }

  preset->content->data[PHATTY_PRESET_ID_OFFSET] = id;
  return common_data_tx (backend, preset->content, control);
}

static gint
phatty_rename (struct backend *backend, const gchar *src, const gchar *dst)
{
  guint id;
  gint err;
  struct job_control control;
  struct sysex_transfer transfer;
  struct idata preset;

  debug_print (1, "Renaming preset...");
  err = common_slot_get_id_name_from_path (src, &id, NULL);
  if (err)
    {
      return err;
    }

  //The control initialization is needed.
  control.active = TRUE;
  control.callback = NULL;
  g_mutex_init (&control.mutex);
  err = phatty_download (backend, src, &preset, &control);
  if (err)
    {
      goto end;
    }

  phatty_set_preset_name (preset.content->data, dst);
  transfer.raw = preset.content;
  err = backend_tx_sysex (backend, &transfer);
  idata_free (&preset);

end:
  return err;
}

static const struct fs_operations FS_PHATTY_PRESET_OPERATIONS = {
  .id = FS_PHATTY_PRESET,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_ALLOW_SEARCH,
  .name = "preset",
  .gui_name = "Presets",
  .gui_icon = FS_ICON_SND,
  .ext = "syx",
  .max_name_len = MOOG_NAME_LEN,
  .readdir = phatty_read_dir,
  .print_item = common_print_item,
  .rename = phatty_rename,
  .download = phatty_download,
  .upload = phatty_upload,
  .get_slot = phatty_get_id_as_slot,
  .load = file_load,
  .save = file_save,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = phatty_get_download_path,
  .select_item = common_midi_program_change
};

static gint
phatty_scale_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar *path, GSList *extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 0;
  data->max = PHATTY_MAX_SCALES;
  iter->data = data;
  iter->next = common_simple_next_dentry;
  iter->free = g_free;

  return 0;
}

static gint
phatty_scale_upload (struct backend *backend, const gchar *path,
		     struct idata *scale, struct job_control *control)
{
  guint id;
  GByteArray *input = scale->content;

  if (common_slot_get_id_name_from_path (path, &id, NULL))
    {
      return -EINVAL;
    }

  input->data[5] = 0;		//bank
  input->data[6] = id;		//scale

  return common_data_tx (backend, input, control);
}

static const struct fs_operations FS_PHATTY_SCALE_OPERATIONS = {
  .id = FS_PHATTY_SCALE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "scale",
  .gui_name = "Scales",
  .gui_icon = FS_ICON_SND,
  .ext = "scl",
  .readdir = phatty_scale_read_dir,
  .print_item = common_print_item,
  .upload = phatty_scale_upload,
  .load = scl_load_2_byte_octave_tuning_msg_from_scala_file,
  .get_upload_path = common_slot_get_upload_path
};

static gint
phatty_handshake (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, MOOG_ID, sizeof (MOOG_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  g_slist_fill (&backend->fs_ops, &FS_PHATTY_PRESET_OPERATIONS,
		&FS_PHATTY_SCALE_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "Moog Little Phatty");

  return 0;
}

const struct connector CONNECTOR_PHATTY = {
  .name = "phatty",
  .handshake = phatty_handshake,
  .standard = TRUE,
  .regex = ".*Phatty.*"
};
