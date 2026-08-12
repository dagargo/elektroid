/*
 *   tanzmaus.c
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
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

#include "default.h"
#include "common.h"

#define MAX_SAMPLES_PER_BANK 16

#define TANZMAUS_SAMPLE_RATE 44000	// This has not published officially.

enum default_fs
{
  FS_TANZMAUS_SAMPLE
};

struct tanzmaus_iterator_data
{
  guint next;
  gint bank;
  struct backend *backend;
};

const guint
tanzmaus_get_sample_len (guint id)
{
  if (id < 4)
    {
      return TANZMAUS_SAMPLE_RATE / 2;
    }
  else if (id < 12)
    {
      return TANZMAUS_SAMPLE_RATE;
    }
  else
    {
      return TANZMAUS_SAMPLE_RATE * 2;
    }
}

static gint
tanzmaus_next_dentry_root (struct item_iterator *iter)
{
  struct tanzmaus_iterator_data *data = iter->data;

  if (data->next < 2)
    {
      iter->item.id = 0x1000 + data->next;	//Unique id
      iter->item.slot[0] = 0;
      item_set_name (&iter->item, "bank %d", data->next + 1);
      iter->item.type = ITEM_TYPE_DIR;
      iter->item.size = -1;

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
tanzmaus_next_dentry_bank (struct item_iterator *iter)
{
  struct tanzmaus_iterator_data *data = iter->data;

  if (data->next < MAX_SAMPLES_PER_BANK)
    {
      iter->item.id = (data->bank * MAX_SAMPLES_PER_BANK) + data->next;
      common_slot_set_slot_padded (&iter->item, 2);
      guint len = tanzmaus_get_sample_len (data->next);
      guint s = len / (double) TANZMAUS_SAMPLE_RATE;
      item_set_name (&iter->item, "%.*d (%.1f s)", 2, data->next + 1, s);
      iter->item.type = ITEM_TYPE_FILE;
      iter->item.size = len * 2;

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
tanzmaus_read_dir (struct backend *backend, struct item_iterator *iter,
		   const gchar *dir, const gchar **extensions)
{
  if (!strcmp (dir, "/"))
    {
      struct tanzmaus_iterator_data *data =
	g_malloc (sizeof (struct tanzmaus_iterator_data));
      data->next = 0;
      data->bank = -1;
      data->backend = backend;
      item_iterator_init (iter, dir, data, tanzmaus_next_dentry_root, g_free);
      return 0;
    }
  else if (!strcmp (dir, "/bank 1"))
    {
      struct tanzmaus_iterator_data *data =
	g_malloc (sizeof (struct tanzmaus_iterator_data));
      data->next = 0;
      data->bank = 0;
      data->backend = backend;
      item_iterator_init (iter, dir, data, tanzmaus_next_dentry_bank, g_free);
      return 0;
    }
  else if (!strcmp (dir, "/bank 2"))
    {
      struct tanzmaus_iterator_data *data =
	g_malloc (sizeof (struct tanzmaus_iterator_data));
      data->next = 0;
      data->bank = 1;
      data->backend = backend;
      item_iterator_init (iter, dir, data, tanzmaus_next_dentry_bank, g_free);
      return 0;
    }
  else
    {
      return -ENOTDIR;
    }
}

static gint
tanzmaus_sample_upload (struct backend *backend, const gchar *path,
			struct idata *sample, struct task_control *control)
{
  // TODO
  return -1;
}

static gint
tanzmaus_sample_load (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  return common_sample_load (path, sample, control, 1, TANZMAUS_SAMPLE_RATE,
			     SF_FORMAT_PCM_16, FALSE);
}

const struct fs_operations FS_TANZMAUS_SAMPLE_OPERATIONS = {
  .id = FS_TANZMAUS_SAMPLE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = tanzmaus_read_dir,
  .load = tanzmaus_sample_upload,
  .load = tanzmaus_sample_load,
  .get_exts = sample_get_sample_extensions
};

static gint
tanzmaus_handshake (struct backend *backend)
{
  // TODO
  gslist_fill (&backend->fs_ops, &FS_TANZMAUS_SAMPLE_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "%s", "MFB Tanzmaus");
  return 0;
}

const struct connector CONNECTOR_TANZMAUS = {
  .name = "tanzmaus",
  .handshake = tanzmaus_handshake,
  .type = CONNECTOR_TYPE_MIDI
};
