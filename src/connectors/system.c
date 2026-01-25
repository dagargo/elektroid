/*
 *   system.c
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

#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#if defined(__linux__)
#include <sys/statvfs.h>
#include <mntent.h>
#endif
#include "local.h"
#include "sample.h"
#include "connectors/common.h"

struct system_iterator_data
{
  GDir *dir;
  const gchar **extensions;
};

static gint
system_download (struct backend *backend, const gchar *path,
		 struct idata *idata, struct task_control *control)
{
  gint err;

  task_control_reset (control, 1);

  err = file_load (path, idata, control);

  if (!err)
    {
      task_control_set_progress (control, 1.0);
    }

  return err;
}

gint
system_mkdir (struct backend *backend, const gchar *name)
{
  gint res = g_mkdir_with_parents (name, 0755);
  if (res == 0 || errno == EEXIST)
    {
      res = 0;
    }
  else
    {
      error_print ("Error while creating dir %s", name);
      res = -errno;
    }

  return res;
}

gint
system_delete (struct backend *backend, const gchar *path)
{
  GDir *dir;
  gchar *new_path;

  if ((dir = g_dir_open (path, 0, NULL)))
    {
      debug_print (1, "Deleting local %s dir...", path);

      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
	{
	  new_path = path_chain (PATH_SYSTEM, path, name);
	  system_delete (backend, new_path);
	  g_free (new_path);
	}

      g_dir_close (dir);

      return rmdir (path);
    }
  else
    {
      debug_print (1, "Deleting local %s file...", path);
      return g_unlink (path);
    }
}

gint
system_rename (struct backend *backend, const gchar *old, const gchar *new)
{
  debug_print (1, "Renaming locally from %s to %s...", old, new);
  return rename (old, new);
}

static void
system_free_iterator_data (void *iter_data)
{
  struct system_iterator_data *data = iter_data;
  g_dir_close (data->dir);
  g_free (data);
}

static gint
system_next_dentry (struct item_iterator *iter, gboolean sample_info)
{
  gint err;
  gchar *full_path;
  const gchar *name;
  struct system_iterator_data *data = iter->data;

  err = -ENOENT;
  while ((name = g_dir_read_name (data->dir)) != NULL)
    {
      if (name[0] == '.')
	{
	  continue;
	}

      full_path = path_chain (PATH_SYSTEM, iter->dir, name);
      enum item_type type;
      if (g_file_test (full_path, G_FILE_TEST_IS_DIR))
	{
	  type = ITEM_TYPE_DIR;
	}
      else if (g_file_test (full_path, G_FILE_TEST_IS_REGULAR))
	{
	  type = ITEM_TYPE_FILE;
	}
      else
	{
	  error_print ("'%s' is neither file nor directory", full_path);
	  continue;
	}

      GFile *file = g_file_new_for_path (full_path);
      GError *error = NULL;
      GFileInfo *info = g_file_query_info (file, "standard::*",
					   G_FILE_QUERY_INFO_NONE, NULL,
					   &error);
      if (error == NULL)
	{
	  item_set_name (&iter->item, "%s", name);
	  iter->item.type = type;
	  iter->item.size = g_file_info_get_size (info);
	  iter->item.id = -1;

	  if (item_iterator_is_dir_or_matches_exts (iter, data->extensions))
	    {
	      if (iter->item.type == ITEM_TYPE_FILE && sample_info)
		{
		  sample_load_sample_info (full_path,
					   &iter->item.sample_info);
		}
	      err = 0;
	    }

	  g_object_unref (info);
	}

      g_object_unref (file);
      g_free (full_path);

      if (!err)
	{
	  break;
	}
    }

  return err;
}

static gint
system_next_dentry_without_sample_info (struct item_iterator *iter)
{
  return system_next_dentry (iter, FALSE);
}

static gint
system_next_dentry_with_sample_info (struct item_iterator *iter)
{
  return system_next_dentry (iter, TRUE);
}

#if defined(__MINGW32__) | defined(__MINGW64__)
struct windows_drive_iterator_data
{
  GList *mounts;
  GList *next;
};

static int
windows_drive_next_dentry (struct item_iterator *iter)
{
  struct windows_drive_iterator_data *data = iter->data;

  if (!data->next)
    {
      return -ENOENT;
    }

  GMount *gmount = data->next->data;
  GFile *gfile = g_mount_get_root (gmount);
  gchar *name = g_file_get_path (gfile);
  g_object_unref (gfile);
  item_set_name (&iter->item, "%s", name);
  g_free (name);
  iter->item.type = ITEM_TYPE_DIR;
  iter->item.size = -1;
  iter->item.id = -1;

  data->next = data->next->next;

  return 0;
}

static void
windows_drive_free_iterator_data (void *iter_data)
{
  struct windows_drive_iterator_data *data = iter_data;
  g_list_free_full (data->mounts, g_object_unref);
  g_free (data);
}
#endif

static gint
system_read_dir_opts (struct backend *backend, struct item_iterator *iter,
		      const gchar *dir, const gchar **extensions,
		      iterator_next next)
{
#if defined(__MINGW32__) | defined(__MINGW64__)
  if (!strcmp (dir, TOPMOST_DIR_WINDOWS))
    {
      GVolumeMonitor *monitor = g_volume_monitor_get ();
      struct windows_drive_iterator_data *data;

      data = g_malloc (sizeof (struct windows_drive_iterator_data));
      data->mounts = g_volume_monitor_get_mounts (monitor);
      data->next = data->mounts;

      item_iterator_init (iter, dir, data, windows_drive_next_dentry,
			  windows_drive_free_iterator_data);

      return 0;
    }
#endif

  GDir *gdir;

  if (!(gdir = g_dir_open (dir, 0, NULL)))
    {
      return -errno;
    }

  struct system_iterator_data *data;
  data = g_malloc (sizeof (struct system_iterator_data));
  data->dir = gdir;
  data->extensions = extensions;

  item_iterator_init (iter, dir, data, next, system_free_iterator_data);

  return 0;
}

gint
system_read_dir (struct backend *backend, struct item_iterator *iter,
		 const gchar *dir, const gchar **extensions)
{
  return system_read_dir_opts (backend, iter, dir, extensions,
			       system_next_dentry_without_sample_info);
}

gint
system_samples_read_dir (struct backend *backend, struct item_iterator *iter,
			 const gchar *dir, const gchar **extensions)
{
  return system_read_dir_opts (backend, iter, dir, extensions,
			       system_next_dentry_with_sample_info);
}

static gint
system_load_custom (const gchar *path, struct idata *sample,
		    struct task_control *control, guint32 channels,
		    guint32 rate, guint32 format)
{
  struct sample_info sample_info_src;
  struct sample_load_opts opts;
  sample_load_opts_init (&opts, channels, rate, format, FALSE);
  //Typically, control parts are set not here but in this case makes more sense.
  control->parts = 1;
  control->part = 0;
  return sample_load_from_file (path, sample, control, &opts,
				&sample_info_src);
}

static gint
system_load_stereo_48k_16b (const gchar *path, struct idata *sample,
			    struct task_control *control)
{
  return system_load_custom (path, sample, control, 2, 48000,
			     SF_FORMAT_PCM_16);
}

gint
system_upload (struct backend *backend, const gchar *path,
	       struct idata *sample, struct task_control *control)
{
  //Typically, control parts are set here but in this case makes more sense do it in the load functions.
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static gint
system_load_mono_48k_16b (const gchar *path, struct idata *sample,
			  struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 48000,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_stereo_44k1_16b (const gchar *path, struct idata *sample,
			     struct task_control *control)
{
  return system_load_custom (path, sample, control, 2, 44100,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_mono_44k1_16b (const gchar *path, struct idata *sample,
			   struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 44100,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_stereo_44k1_24b (const gchar *path, struct idata *sample,
			     struct task_control *control)
{
  return system_load_custom (path, sample, control, 2, 44100,
			     SF_FORMAT_PCM_32);
}

static gint
system_upload_24_bits (struct backend *backend, const gchar *path,
		       struct idata *sample, struct task_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_24);
}

static gint
system_load_mono_44k1_24b (const gchar *path, struct idata *sample,
			   struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 44100,
			     SF_FORMAT_PCM_32);
}

static gint
system_upload_8_bits (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_U8);
}

static gint
system_load_mono_32k_16b (const gchar *path, struct idata *sample,
			  struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 32000,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_mono_31k25_16b (const gchar *path, struct idata *sample,
			    struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 31250,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_mono_22k05_16b (const gchar *path, struct idata *sample,
			    struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 22050,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_mono_16k_16b (const gchar *path, struct idata *sample,
			  struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 16000,
			     SF_FORMAT_PCM_16);
}

static gint
system_load_mono_8k_16b (const gchar *path, struct idata *sample,
			 struct task_control *control)
{
  return system_load_custom (path, sample, control, 1, 8000,
			     SF_FORMAT_PCM_16);
}

gboolean
system_file_exists (struct backend *backend, const gchar *path)
{
  return access (path, F_OK) == 0;
}

enum system_fs
{
  FS_SYSTEM_SAMPLES_STEREO_48K_16B,
  FS_SYSTEM_SAMPLES_MONO_48K_16B,
  FS_SYSTEM_SAMPLES_STEREO_44K1_16B,
  FS_SYSTEM_SAMPLES_MONO_44K1_16B,
  FS_SYSTEM_SAMPLES_STEREO_44K1_24B,
  FS_SYSTEM_SAMPLES_MONO_44K1_24B,
  FS_SYSTEM_SAMPLES_STEREO_441K_8B,
  FS_SYSTEM_SAMPLES_MONO_44K1_8B,
  FS_SYSTEM_SAMPLES_MONO_32K_16B,
  FS_SYSTEM_SAMPLES_MONO_31K25_16B,
  FS_SYSTEM_SAMPLES_MONO_22K05_16B,
  FS_SYSTEM_SAMPLES_MONO_16K_16B,
  FS_SYSTEM_SAMPLES_MONO_16K_8B,
  FS_SYSTEM_SAMPLES_MONO_8K_16B,
  FS_SYSTEM_SAMPLES_MONO_8K_8B,
};

const struct fs_operations FS_SYSTEM_SAMPLES_STEREO_48K_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_STEREO_48K_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-stereo-48k-16b",
  .gui_name = "WAV stereo 48 kHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_stereo_48k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_48K_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_48K_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-48k-16b",
  .gui_name = "WAV mono 48 kHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_48k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_STEREO_44K1_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_STEREO_44K1_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-stereo-44k1-16b",
  .gui_name = "WAV stereo 44.1 kHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_stereo_44k1_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_44K1_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_44K1_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-44k1-16b",
  .gui_name = "WAV mono 44.1 kHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_44k1_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_STEREO_44K1_24B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_STEREO_44K1_24B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-stereo-44k1-24b",
  .gui_name = "WAV stereo 44.1 kHz 24 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_24_bits,
  .load = system_load_stereo_44k1_24b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_44K1_24B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_44K1_24B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-44k1-24b",
  .gui_name = "WAV mono 44.1 kHz 24 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_24_bits,
  .load = system_load_mono_44k1_24b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_STEREO_441K_8B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_STEREO_441K_8B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-stereo-44k1-8b",
  .gui_name = "WAV stereo 44.1 kHz 8 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_stereo_44k1_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_44K1_8B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_44K1_8B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-44k1-8b",
  .gui_name = "WAV mono 44.1 kHz 8 bit",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_mono_44k1_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_32K_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_32K_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-32k-16b",
  .gui_name = "WAV mono 32 kHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_32k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_31K25_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_31K25_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-31k25-16b",
  .gui_name = "WAV mono 31.25 KHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_31k25_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_22K05_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_22K05_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-22k05-16b",
  .gui_name = "WAV mono 22.05 KHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_22k05_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_16K_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_16K_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-16k-16b",
  .gui_name = "WAV mono 16 KHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_16k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_16K_8B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_16K_8B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-16k-8b",
  .gui_name = "WAV mono 16 KHz 8 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_mono_16k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_8K_16B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_8K_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-8k-16b",
  .gui_name = "WAV mono 8 KHz 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_mono_8k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_MONO_8K_8B_OPERATIONS = {
  .id = FS_SYSTEM_SAMPLES_MONO_8K_8B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav-mono-8k-8b",
  .gui_name = "WAV mono 8 KHz 8 bits",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_mono_8k_16b,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

#if defined (__linux__)
static gint
system_get_storage_stats (struct backend *backend, guint8 type,
			  struct backend_storage_stats *statfs,
			  const gchar *path)
{
  gint err;
  FILE *f;
  struct statvfs svfs;
  struct stat s1, s2;
  struct mntent *me;

  if ((err = stat (path, &s1)) < 0)
    {
      return err;
    }

  f = setmntent ("/proc/mounts", "r");
  if (f == NULL)
    {
      return -ENODEV;
    }
  while ((me = getmntent (f)))
    {
      if (!stat (me->mnt_dir, &s2))
	{
	  if (s1.st_dev == s2.st_dev)
	    {
	      break;
	    }
	}
    }
  endmntent (f);

  err = statvfs (path, &svfs);
  if (!err)
    {
      snprintf (statfs->name, LABEL_MAX, "%s", me->mnt_fsname);
      statfs->bfree = svfs.f_bavail * svfs.f_frsize;
      statfs->bsize = svfs.f_blocks * svfs.f_frsize;
    }

  return err;
}
#endif

static gint
system_handshake (struct backend *backend)
{
  gslist_fill (&backend->fs_ops, &FS_SYSTEM_SAMPLES_STEREO_48K_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_48K_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_STEREO_44K1_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_44K1_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_STEREO_44K1_24B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_44K1_24B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_STEREO_441K_8B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_44K1_8B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_32K_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_31K25_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_22K05_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_16K_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_16K_8B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_8K_16B_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_MONO_8K_8B_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "%s", _("System"));

  backend->get_storage_stats =
#if defined (__linux__)
    system_get_storage_stats;
#else
    NULL;
#endif

  return 0;
}

const struct connector CONNECTOR_SYSTEM = {
  .handshake = system_handshake,
  .name = "system",
  .options = CONNECTOR_OPTION_CUSTOM_HANDSHAKE,
  .regex = NULL
};
