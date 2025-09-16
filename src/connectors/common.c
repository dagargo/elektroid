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
#include "sample.h"

static const gchar *SYSEX_EXTS[] = { "syx", NULL };

static void
common_replace_chars (gchar *str, gchar x, gchar y)
{
  gchar *c = str;
  while (*c)
    {
      if (*c == x)
	{
	  *c = y;
	}
      c++;
    }
}

//These conversions depend on the OS but its safer and simpler to apply this restrictions to all of them.
void
common_to_os_sanitized_name (gchar *name)
{
  common_replace_chars (name, '/', '?');
  common_replace_chars (name, '\\', '?');
}

const gchar **
common_sysex_get_extensions (struct backend *backend,
			     const struct fs_operations *ops)
{
  return SYSEX_EXTS;
}

const gchar **
common_get_all_extensions (struct backend *backend,
			   const struct fs_operations *ops)
{
  return NULL;
}

gchar *
common_slot_get_upload_path (struct backend *backend,
			     const struct fs_operations *ops,
			     const gchar *dst_path, const gchar *src_path,
			     struct idata *content)
{
  //In SLOT mode, dst_path points to a slot not to a directory
  return strdup (dst_path);
}

gint
common_slot_get_id_from_path (const char *path, guint *id)
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
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  gint max_name_len = fs_ops->max_name_len ? fs_ops->max_name_len :
    DEFAULT_MAX_NAME_LEN;
  gboolean info = (fs_ops->options & FS_OPTION_SHOW_INFO_COLUMN) &&
    *iter->item.object_info;

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

  printf ("%c %10s %.*s%s%-*s%s%s%s\n", iter->item.type, hsize,
	  slot ? 10 : 0, slot, slot ? " " : "",
	  info ? max_name_len : (gint) strlen (iter->item.name),
	  iter->item.name, info ? " [ " : "",
	  info ? iter->item.object_info : "", info ? " ]" : "");

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
  guint digits = ((guint) floor (log10 (data->last))) + 1;

  if (data->next > data->last)
    {
      return -ENOENT;
    }

  snprintf (iter->item.name, LABEL_MAX, "%.*d", digits, data->next);
  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = -1;
  data->next++;

  return 0;
}

gint
common_data_tx (struct backend *backend, GByteArray *msg,
		struct task_control *control)
{
  gint err = 0;
  struct sysex_transfer transfer;

  sysex_transfer_init_tx (&transfer, msg);

  g_mutex_lock (&backend->mutex);

  task_control_reset (control, 1);

  err = backend_tx_sysex (backend, &transfer, &control->controllable);
  if (err < 0)
    {
      goto cleanup;
    }

  if (controllable_is_active (&control->controllable))
    {
      task_control_set_progress (control, 1.0);
    }
  else
    {
      err = -ECANCELED;
    }

cleanup:
  g_mutex_unlock (&backend->mutex);
  sysex_transfer_steal (&transfer);
  return err;
}

gint
common_data_tx_and_rx_part (struct backend *backend, GByteArray *tx_msg,
			    GByteArray **rx_msg, struct task_control *control)
{
  gint err = 0;

  *rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!*rx_msg)
    {
      err = -EIO;
      goto cleanup;
    }

  task_control_set_progress (control, 1.0);

  if (!controllable_is_active (&control->controllable))
    {
      free_msg (*rx_msg);
      *rx_msg = NULL;
      err = -ECANCELED;
    }

  control->part++;

cleanup:
  return err;
}

gint
common_data_tx_and_rx (struct backend *backend, GByteArray *tx_msg,
		       GByteArray **rx_msg, struct task_control *control)
{
  task_control_reset (control, 1);
  return common_data_tx_and_rx_part (backend, tx_msg, rx_msg, control);
}

static gchar *
common_slot_get_download_path_id_name (struct backend *backend,
				       const struct fs_operations *ops,
				       const gchar *dst_dir,
				       guint id, guint digits,
				       const gchar *name)
{
  gchar *path;
  const gchar *ext = GET_SAVE_EXT (ops, backend);
  GString *str = g_string_new (NULL);

  g_string_append_printf (str, "%s %s", backend->name, ops->name);

  if (digits)
    {
      g_string_append_printf (str, " %.*d", digits, id);
    }

  if (name)
    {
      gchar *sanitized_name = strdup (name);
      common_to_os_sanitized_name (sanitized_name);

      g_string_append (str, " - ");
      g_string_append (str, sanitized_name);

      g_free (sanitized_name);
    }

  g_string_append (str, ".");
  g_string_append (str, ext);

  path = path_chain (PATH_SYSTEM, dst_dir, str->str);
  g_string_free (str, TRUE);

  return path;
}

gchar *
common_slot_get_download_path (struct backend *backend,
			       const struct fs_operations *ops,
			       const gchar *dst_dir,
			       const gchar *src_path,
			       struct idata *idata, guint digits)
{
  guint id;
  if (common_slot_get_id_from_path (src_path, &id))
    {
      return NULL;
    }
  return common_slot_get_download_path_id_name (backend, ops, dst_dir,
						id, digits, idata->name);
}

gchar *
common_slot_get_download_path_n (struct backend *backend,
				 const struct fs_operations *ops,
				 const gchar *dst_dir,
				 const gchar *src_path, struct idata *idata)
{
  return common_slot_get_download_path (backend, ops, dst_dir, src_path,
					idata, 1);
}

gchar *
common_slot_get_download_path_nn (struct backend *backend,
				  const struct fs_operations *ops,
				  const gchar *dst_dir,
				  const gchar *src_path, struct idata *idata)
{
  return common_slot_get_download_path (backend, ops, dst_dir, src_path,
					idata, 2);
}

gchar *
common_slot_get_download_path_nnn (struct backend *backend,
				   const struct fs_operations *ops,
				   const gchar *dst_dir,
				   const gchar *src_path, struct idata *idata)
{
  return common_slot_get_download_path (backend, ops, dst_dir, src_path,
					idata, 3);
}

gchar *
common_get_sanitized_name (const gchar *name, const gchar *alphabet,
			   gchar defchar)
{
  gchar *sanitized = g_str_to_ascii (name, NULL);
  gchar *t, *v;

  if (alphabet)
    {
      t = sanitized;
      while (*t)
	{
	  gboolean valid = FALSE;
	  v = (gchar *) alphabet;
	  while (*v)
	    {
	      if (*t == *v)
		{
		  valid = TRUE;
		  break;
		}
	      v++;
	    }
	  if (!valid)
	    {
	      *t = defchar;
	    }
	  t++;
	}
    }
  return sanitized;
}

gint
common_sample_load (const gchar *path, struct idata *sample,
		    struct task_control *control, guint32 channels,
		    guint32 rate, guint32 format)
{
  struct sample_info sample_info;
  struct sample_load_opts opts;
  sample_load_opts_init (&opts, channels, rate, format);
  return sample_load_from_file (path, sample, control, &opts, &sample_info);
}

gchar *
common_system_get_download_path (struct backend *backend,
				 const struct fs_operations *ops,
				 const gchar *dst_dir, const gchar *src_path,
				 struct idata *idata)
{
  const gchar *ext = GET_SAVE_EXT (ops, backend);
  GString *name_with_ext = g_string_new (NULL);
  g_string_append_printf (name_with_ext, "%s.%s", idata->name, ext);
  gchar *path = path_chain (PATH_SYSTEM, dst_dir, name_with_ext->str);
  g_string_free (name_with_ext, TRUE);
  return path;
}

gchar *
common_system_get_upload_path (struct backend *backend,
			       const struct fs_operations *ops,
			       const gchar *dst_dir,
			       const gchar *src_path, struct idata *content)
{
  return common_system_get_download_path (backend, ops, dst_dir, src_path,
					  content);
}

void
common_midi_msg_to_8bit_msg (guint8 *msg_midi, guint8 *msg_8bit,
			     guint input_size)
{
  guint8 *dst = msg_8bit;
  guint8 *src = msg_midi;
  for (guint i = 0; i < input_size; i++)
    {
      guint8 bits = *src;
      src++;
      for (guint j = 0; j < 7 && i < input_size; j++, i++, src++, dst++)
	{
	  *dst = *src | (bits & 0x1 ? 0x80 : 0);
	  bits >>= 1;
	}
    }
}

void
common_8bit_msg_to_midi_msg (guint8 *msg_8bit, guint8 *msg_midi,
			     guint input_size)
{
  guint8 *dst = msg_midi;
  guint8 *src = msg_8bit;
  guint8 *bits = 0;
  guint rem;
  for (guint i = 0; i < input_size;)
    {
      bits = dst;
      *bits = 0;
      dst++;
      for (guint j = 0; j < 7 && i < input_size; j++, i++, src++, dst++)
	{
	  *dst = *src & 0x7f;
	  *bits |= *src & 0x80;
	  *bits >>= 1;
	}
    }
  rem = input_size % 7;
  if (rem)
    {
      *bits >>= 7 - rem;
    }
}

guint
common_8bit_msg_to_midi_msg_size (guint size)
{
  guint packets = size / 7;
  guint rem = size % 7;
  return packets * 8 + (rem ? rem + 1 : 0);
}

guint
common_midi_msg_to_8bit_msg_size (guint size)
{
  guint packets = size / 8;
  guint rem = size % 8;
  return packets * 7 + (rem ? rem - 1 : 0);
}
