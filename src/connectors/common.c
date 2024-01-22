/*
 *   common.h
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

#include <math.h>
#include "common.h"

gchar *
common_slot_get_upload_path (struct backend *backend,
			     const struct fs_operations *ops,
			     const gchar *dst_path, const gchar *src_path)
{
  //In SLOT mode, dst_path includes the index, ':' and the item name.
  return strdup (dst_path);
}

gint
common_slot_get_id_name_from_path (const char *path, guint *id, gchar **name)
{
  gint err = 0;
  gchar *basename, *remainder;

  basename = g_path_get_basename (path);
  *id = (guint) g_ascii_strtoull (basename, &remainder, 10);
  if (remainder == basename)
    {
      err = -EINVAL;
      goto end;
    }
  if (!id && errno)
    {
      err = -errno;
      goto end;
    }

  if (*remainder == G_SEARCHPATH_SEPARATOR)
    {
      remainder++;		//Skip ':'
    }
  else
    {
      if (name)
	{
	  error_print ("Path name not provided properly\n");
	  err = -EINVAL;
	  goto end;
	}
    }

  if (name)
    {
      if (*remainder)
	{
	  *name = strdup (remainder);
	}
      else
	{
	  *name = NULL;
	}
    }

end:
  g_free (basename);
  return err;
}

gchar *
common_get_id_as_slot_padded (struct item *item, struct backend *backend,
			      gint digits)
{
  gchar *slot = g_malloc (LABEL_MAX);
  snprintf (slot, LABEL_MAX, "%.*d", digits, item->id);
  return slot;
}

gchar *
common_get_id_as_slot (struct item *item, struct backend *backend)
{
  gchar *slot = g_malloc (LABEL_MAX);
  snprintf (slot, LABEL_MAX, "%d", item->id);
  return slot;
}

void
common_print_item (struct item_iterator *iter, struct backend *backend,
		   const struct fs_operations *fs_ops)
{
  gchar *slot = NULL;
  if (fs_ops->options & FS_OPTION_SLOT_STORAGE)
    {
      if (fs_ops->get_slot)
	{
	  slot = fs_ops->get_slot (&iter->item, backend);
	}
      else
	{
	  slot = common_get_id_as_slot (&iter->item, backend);
	}
    }
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  printf ("%c %10s %.*s%s%s\n", iter->item.type, hsize, slot ? 10 : 0,
	  slot, slot ? " " : "", iter->item.name);
  g_free (hsize);
  g_free (slot);
}

void
common_midi_program_change_int (struct backend *backend, const gchar *dir,
				guint32 program)
{
  backend_send_controller (backend, 0, 0, COMMON_GET_MIDI_BANK (program));
  backend_program_change (backend, 0, COMMON_GET_MIDI_PRESET (program));
}

void
common_midi_program_change (struct backend *backend, const gchar *dir,
			    struct item *item)
{
  if (item->id > BE_MAX_MIDI_PROGRAMS)
    {
      return;
    }
  backend_program_change (backend, 0, item->id);
}

gint
common_simple_next_dentry (struct item_iterator *iter)
{
  struct common_simple_read_dir_data *data = iter->data;
  guint digits = floor (log10 (data->max));

  if (data->next >= data->max)
    {
      return -ENOENT;
    }

  snprintf (iter->item.name, LABEL_MAX, "%.*d", digits, data->next);
  iter->item.id = data->next;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  iter->item.slot_used = TRUE;
  data->next++;

  return 0;
}

gint
common_data_upload (struct backend *backend, GByteArray *msg,
		    struct job_control *control)
{
  gint err = 0;
  gboolean active;
  struct sysex_transfer transfer;

  g_mutex_lock (&backend->mutex);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  transfer.raw = msg;
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


gint
common_data_download_part (struct backend *backend, GByteArray *tx_msg,
			   GByteArray **rx_msg, struct job_control *control)
{
  gint err = 0;
  gboolean active;

  set_job_control_progress (control, 0.0);

  *rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!*rx_msg)
    {
      err = -EIO;
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
      free_msg (*rx_msg);
      *rx_msg = NULL;
      err = -ECANCELED;
    }

cleanup:
  return err;
}

gint
common_data_download (struct backend *backend, GByteArray *tx_msg,
		      GByteArray **rx_msg, struct job_control *control)
{
  control->parts = 1;
  control->part = 0;
  return common_data_download_part (backend, tx_msg, rx_msg, control);
}

gchar *
common_get_download_path_with_params (struct backend *backend,
				      const struct fs_operations *ops,
				      const gchar *dst_dir,
				      guint id, guint digits,
				      const gchar *name)
{
  gchar *path;
  GString *str = g_string_new (NULL);
  g_string_append_printf (str, "%s %s %.*d", backend->name, ops->name,
			  digits, id);
  if (name)
    {
      g_string_append (str, " - ");
      g_string_append (str, name);
    }
  g_string_append (str, ".");
  g_string_append (str, ops->type_ext);

  path = path_chain (PATH_SYSTEM, dst_dir, str->str);
  g_string_free (str, TRUE);

  return path;
}

gchar *
common_get_download_path (struct backend *backend,
			  const struct fs_operations *ops,
			  const gchar *dst_dir, const gchar *src_path,
			  GByteArray *sysex)
{
  guint id;
  common_slot_get_id_name_from_path (src_path, &id, NULL);
  return common_get_download_path_with_params (backend, ops, dst_dir,
					       id, 3, NULL);
}

void
common_remove_slot_name_from_path (gchar *path)
{
  gchar *c;
  gint i, len = strlen (path);
  if (len == 0)
    {
      return;
    }
  i = len - 1;
  c = path + i;
  while (i >= 0 && *c != '/')
    {
      if (*c == ':')
	{
	  *c = 0;
	  break;
	}
      c--;
      i--;
    }
}
