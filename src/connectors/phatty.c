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

#include <libgen.h>
#include "phatty.h"
#include "common.h"

#define PHATTY_ALPHABET " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz!#$%&()*?@"

#define PHATTY_MAX_PRESETS 100
#define PHATTY_PROGRAM_SIZE 193

#define PHATTY_PRESET_ID_OFFSET 5

#define MOOG_NAME_LEN 13

#define PHATTY_PRESETS_DIR "/presets"
#define PHATTY_PANEL "panel"
#define PHATTY_PANEL_PATH "/" PHATTY_PANEL
#define PHATTY_PANEL_ID 0x100

static const guint8 MOOG_ID[] = { 0x04 };
static const guint8 FAMILY_ID[] = { 0x0, 0x5 };
static const guint8 MODEL_ID[] = { 0x0, 0x1 };

static const guint8 PHATTY_REQUEST_PANEL[] =
  { 0xf0, 4, 5, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };
static const guint8 PHATTY_REQUEST_PRESET[] =
  { 0xf0, 4, 5, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };

struct phatty_data
{
  guint8 presets[PHATTY_MAX_PRESETS][PHATTY_PROGRAM_SIZE];
};

struct phatty_iter_data
{
  guint next;
  struct phatty_data *backend_data;
};

enum phatty_fs
{
  FS_PHATTY_PRESET = 1
};

static gchar
phatty_decode_char (guint8 * data, gint position)
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
phatty_encode_char (guint8 * data, gchar c, gint position)
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
phatty_set_preset_name (guint8 * preset, const gchar * preset_name)
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
phatty_get_preset_name (guint8 * preset, gchar * preset_name)
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
			  const gchar * dst_dir, const gchar * src_path)
{
  struct phatty_data *data;
  struct item_iterator iter;
  gchar preset_name[MOOG_NAME_LEN + 1];
  gint err = 0;
  gchar *name = malloc (PATH_MAX);
  gchar *src_path_copy = strdup (src_path);
  gchar *filename = basename (src_path_copy);
  gint id = atoi (filename);
  g_free (src_path_copy);

  if (id == PHATTY_PANEL_ID)
    {
      snprintf (name, PATH_MAX, "%s/Moog Little Phatty %s.syx", dst_dir,
		PHATTY_PANEL);
      return name;
    }

  if (!backend->data)
    {
      err = ops->readdir (backend, &iter, PHATTY_PRESETS_DIR);
      if (err)
	{
	  return NULL;
	}
      free_item_iterator (&iter);
    }
  data = backend->data;
  phatty_get_preset_name (data->presets[id], preset_name);
  snprintf (name, PATH_MAX, "%s/Moog Little Phatty %02d %s.syx", dst_dir, id,
	    preset_name);

  return name;
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

static guint
phatty_next_root_dentry (struct item_iterator *iter)
{
  guint *next = iter->data;
  if (*next == 0)
    {
      snprintf (iter->item.name, LABEL_MAX, "%s", "presets");
      iter->item.id = 0x1000;
      iter->item.type = ELEKTROID_DIR;
      iter->item.size = -1;
    }
  else if (*next == 1)
    {
      snprintf (iter->item.name, LABEL_MAX, "%s", "panel");
      iter->item.id = PHATTY_PANEL_ID;
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = -1;
    }
  else
    {
      return -ENOENT;
    }

  (*next)++;

  return 0;
}

static guint
phatty_next_preset_dentry (struct item_iterator *iter)
{
  gchar preset_name[MOOG_NAME_LEN + 1];
  struct phatty_iter_data *data = iter->data;
  struct phatty_data *backend_data = data->backend_data;

  if (data->next >= PHATTY_MAX_PRESETS)
    {
      return -ENOENT;
    }

  phatty_get_preset_name (backend_data->presets[data->next], preset_name);
  snprintf (iter->item.name, LABEL_MAX, "%s", preset_name);
  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = PHATTY_PROGRAM_SIZE;
  (data->next)++;

  return 0;
}

static gint
phatty_read_dir (struct backend *backend, struct item_iterator *iter,
		 const gchar * path)
{
  gint err = 0;
  GByteArray *tx_msg, *rx_msg;
  struct phatty_data *data;
  struct phatty_iter_data *iter_data;

  if (!strcmp (path, "/"))
    {
      guint *id = g_malloc (sizeof (guint));
      *id = 0;
      iter->data = id;
      iter->next = phatty_next_root_dentry;
      iter->free = g_free;
    }
  else if (!strcmp (path, PHATTY_PRESETS_DIR))
    {
      if (backend->data)
	{
	  data = backend->data;
	}
      else
	{
	  data = g_malloc (sizeof (struct phatty_data));
	  backend->data = data;
	}

      for (gint i = 0; i < PHATTY_MAX_PRESETS; i++)
	{
	  tx_msg = phatty_get_preset_dump_msg (i);
	  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
					    BE_SYSEX_TIMEOUT_GUESS_MS);
	  if (!rx_msg)
	    {
	      return -EIO;
	    }

	  memcpy (data->presets[i], rx_msg->data, rx_msg->len);
	  free_msg (rx_msg);
	}

      iter_data = g_malloc (sizeof (struct phatty_iter_data));
      iter_data->next = 0;
      iter_data->backend_data = data;
      iter->data = iter_data;
      iter->next = phatty_next_preset_dentry;
      iter->free = g_free;
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
  gchar *slot = malloc (LABEL_MAX);
  if (item->id >= PHATTY_MAX_PRESETS)
    {
      slot[0] = 0;
    }
  else
    {
      snprintf (slot, LABEL_MAX, "%d", item->id);
    }
  return slot;
}

static gint
phatty_download (struct backend *backend, const gchar * path,
		 GByteArray * output, struct job_control *control)
{
  guint8 id;
  gboolean active;
  gint len, err = 0;
  gchar *basename_copy;
  GByteArray *tx_msg, *rx_msg;

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  if (strcmp (path, PHATTY_PANEL_PATH))
    {
      basename_copy = strdup (path);
      id = atoi (basename (basename_copy));
      g_free (basename_copy);
      tx_msg = phatty_get_preset_dump_msg (id);
    }
  else
    {
      tx_msg = phatty_get_panel_dump_msg ();
    }

  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = rx_msg->len;
  if (len != PHATTY_PROGRAM_SIZE)
    {
      err = -EINVAL;
      goto cleanup;
    }

  g_byte_array_append (output, rx_msg->data, rx_msg->len);

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
  return err;
}

static gint
phatty_upload (struct backend *backend, const gchar * path,
	       GByteArray * input, struct job_control *control)
{
  guint id;
  gint err;
  gboolean active;
  struct sysex_transfer transfer;

  if (common_slot_get_id_name_from_path (path, &id, NULL))
    {
      return -EINVAL;
    }

  if (!strcmp (path, PHATTY_PANEL_PATH))
    {
      return -EINVAL;
    }

  g_mutex_lock (&backend->mutex);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  input->data[PHATTY_PRESET_ID_OFFSET] = id;
  transfer.raw = input;
  err = backend_tx_sysex (backend, &transfer);
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
  return err;
}

static gint
phatty_rename (struct backend *backend, const gchar * src, const gchar * dst)
{
  guint id;
  gint err;
  struct job_control control;
  struct sysex_transfer transfer;

  debug_print (1, "Renaming preset...\n");
  err = common_slot_get_id_name_from_path (src, &id, NULL);
  if (err)
    {
      return err;
    }

  transfer.raw = g_byte_array_new ();
  //The control initialization is needed.
  control.active = TRUE;
  control.callback = NULL;
  g_mutex_init (&control.mutex);
  err = phatty_download (backend, src, transfer.raw, &control);
  if (err)
    {
      goto end;
    }

  phatty_set_preset_name (transfer.raw->data, dst);
  err = backend_tx_sysex (backend, &transfer);

end:
  free_msg (transfer.raw);
  return err;
}

static const struct fs_operations FS_PHATTY_OPERATIONS = {
  .fs = FS_PHATTY_PRESET,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "preset",
  .gui_name = "Presets",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "syx",
  .max_name_len = MOOG_NAME_LEN,
  .readdir = phatty_read_dir,
  .print_item = common_print_item,
  .rename = phatty_rename,
  .download = phatty_download,
  .upload = phatty_upload,
  .get_slot = phatty_get_id_as_slot,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = phatty_get_download_path
};

static const struct fs_operations *FS_PHATTY_OPERATIONS_LIST[] = {
  &FS_PHATTY_OPERATIONS, NULL
};

gint
phatty_handshake (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, MOOG_ID, sizeof (MOOG_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  backend->device_desc.filesystems = FS_PHATTY_PRESET;
  backend->fs_ops = FS_PHATTY_OPERATIONS_LIST;
  backend->data = NULL;
  backend->destroy_data = backend_destroy_data;

  snprintf (backend->device_name, LABEL_MAX, "Moog Little Phatty %d.%d.%d.%d",
	    backend->midi_info.version[0], backend->midi_info.version[1],
	    backend->midi_info.version[2], backend->midi_info.version[3]);

  return 0;
}
