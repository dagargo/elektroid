/*
 *   local.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include "local.h"
#include "sample.h"

struct local_iterator_data
{
  DIR *dir;
  gchar *path;
  const gchar **extensions;
};

static gint
local_download (struct backend *backend, const gchar * path,
		GByteArray * output, struct job_control *control)
{
  gint err;
  gboolean active;

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  err = load_file (path, output, control);

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

  return err;
}

static gchar *
local_get_download_path (struct backend *backend,
			 const struct fs_operations *ops,
			 const gchar * dst_dir, const gchar * src_path,
			 GByteArray * content)
{
  gchar *name = g_path_get_basename (src_path);
  GString *name_with_ext = g_string_new (NULL);
  remove_ext (name);
  g_string_append_printf (name_with_ext, "%s.%s", name, ops->type_ext);
  gchar *path = path_chain (PATH_SYSTEM, dst_dir, name_with_ext->str);
  g_string_free (name_with_ext, TRUE);
  return path;
}

static gchar *
local_get_upload_path (struct backend *backend,
		       const struct fs_operations *ops,
		       const gchar * dst_dir, const gchar * src_path)
{
  return local_get_download_path (backend, ops, dst_dir, src_path, NULL);
}

gint
local_mkdir (struct backend *backend, const gchar * name)
{
  gint res = g_mkdir_with_parents (name, 0755);
  if (res == 0 || errno == EEXIST)
    {
      res = 0;
    }
  else
    {
      error_print ("Error while creating dir %s\n", name);
      res = -errno;
    }

  return res;
}

static gint
local_delete (struct backend *backend, const gchar * path)
{
  DIR *dir;
  gchar *new_path;
  struct dirent *dirent;

  if ((dir = opendir (path)))
    {
      debug_print (1, "Deleting local %s dir...\n", path);

      while ((dirent = readdir (dir)) != NULL)
	{
	  if (strcmp (dirent->d_name, ".") == 0 ||
	      strcmp (dirent->d_name, "..") == 0)
	    {
	      continue;
	    }
	  new_path = path_chain (PATH_SYSTEM, path, dirent->d_name);
	  local_delete (backend, new_path);
	  g_free (new_path);
	}

      closedir (dir);

      return rmdir (path);
    }
  else
    {
      debug_print (1, "Deleting local %s file...\n", path);
      return unlink (path);
    }
}

static gint
local_rename (struct backend *backend, const gchar * old, const gchar * new)
{
  debug_print (1, "Renaming locally from %s to %s...\n", old, new);
  return rename (old, new);
}

static void
local_free_iterator_data (void *iter_data)
{
  struct local_iterator_data *data = iter_data;
  closedir (data->dir);
  g_free (data->path);
  g_free (data);
}

static guint
local_next_dentry (struct item_iterator *iter, gboolean sample_info)
{
  gchar *full_path;
  struct dirent *dirent;
  struct stat st;
  mode_t mode;
  struct local_iterator_data *data = iter->data;

  while ((dirent = readdir (data->dir)) != NULL)
    {
      if (dirent->d_name[0] == '.')
	{
	  continue;
	}

      full_path = path_chain (PATH_SYSTEM, data->path, dirent->d_name);
      if (stat (full_path, &st))
	{
	  g_free (full_path);
	  continue;
	}

      mode = st.st_mode & S_IFMT;
      switch (mode)
	{
	case S_IFREG:
	case S_IFDIR:
	  snprintf (iter->item.name, LABEL_MAX, "%s", dirent->d_name);
	  iter->item.type = mode == S_IFREG ? ELEKTROID_FILE : ELEKTROID_DIR;
	  iter->item.size = st.st_size;
	  iter->item.id = -1;
	  break;
	default:
	  error_print ("stat mode neither file nor directory for %s\n",
		       full_path);
	  continue;
	}

      if (iter_is_dir_or_matches_extensions (iter, data->extensions))
	{
	  if (iter->item.type == ELEKTROID_FILE && sample_info)
	    {
	      sample_load_sample_info (full_path, &iter->item.sample_info);
	    }
	  g_free (full_path);
	  return 0;
	}

      g_free (full_path);
    }

  return -ENOENT;
}

static guint
local_next_dentry_without_sample_info (struct item_iterator *iter)
{
  return local_next_dentry (iter, FALSE);
}

static guint
local_next_dentry_with_sample_info (struct item_iterator *iter)
{
  return local_next_dentry (iter, TRUE);
}

static gint
local_read_dir (struct backend *backend, struct item_iterator *iter,
		const gchar * path, const gchar ** extensions)
{
  DIR *dir;
  struct local_iterator_data *data;

  if (!(dir = opendir (path)))
    {
      return -errno;
    }

  data = g_malloc (sizeof (struct local_iterator_data));
  data->dir = dir;
  data->path = strdup (path);
  data->extensions = extensions;

  iter->data = data;
  iter->next = local_next_dentry_without_sample_info;
  iter->free = local_free_iterator_data;

  return 0;
}

static gint
local_samples_read_dir (struct backend *backend, struct item_iterator *iter,
			const gchar * path, const gchar ** extensions)
{
  gint ret = local_read_dir (backend, iter, path,
			     sample_get_sample_extensions ());
  if (!ret)
    {
      iter->next = local_next_dentry_with_sample_info;
    }
  return ret;
}

static gint
local_sample_load_custom (const gchar * path, GByteArray * sample,
			  struct job_control *control,
			  struct sample_info *sample_info_dst)
{
  control->parts = 1;
  control->part = 0;
  gint res = sample_load_from_file (path, sample, control, sample_info_dst);
  if (!res)
    {
      memcpy (control->data, sample_info_dst, sizeof (struct sample_info));
    }
  return res;
}

static gint
local_sample_load_48_16_stereo (const gchar * path, GByteArray * sample,
				struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 48000;
  sample_info_dst.channels = 2;
  return local_sample_load_custom (path, sample, control, &sample_info_dst);
}

static gint
local_sample_load_48_16_mono (const gchar * path, GByteArray * sample,
			      struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 48000;
  sample_info_dst.channels = 1;
  return local_sample_load_custom (path, sample, control, &sample_info_dst);
}

static gint
local_upload (struct backend *backend, const gchar * path, GByteArray * input,
	      struct job_control *control)
{
  return sample_save_from_array (path, input, control);
}

static gint
local_upload_48_16_stereo (struct backend *backend, const gchar * path,
			   GByteArray * input, struct job_control *control)
{
  struct sample_info *sample_info_dst = control->data;
  sample_info_dst->rate = 48000;
  sample_info_dst->channels = 2;
  return sample_save_from_array (path, input, control);
}

static gint
local_upload_48_16_mono (struct backend *backend, const gchar * path,
			 GByteArray * input, struct job_control *control)
{
  struct sample_info *sample_info_dst = control->data;
  sample_info_dst->rate = 48000;
  sample_info_dst->channels = 1;
  return sample_save_from_array (path, input, control);
}

static gint
local_sample_load_441_16_stereo (const gchar * path, GByteArray * sample,
				 struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 2;
  return local_sample_load_custom (path, sample, control, &sample_info_dst);
}

static gint
local_sample_load_441_16_mono (const gchar * path, GByteArray * sample,
			       struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  return local_sample_load_custom (path, sample, control, &sample_info_dst);
}

static gint
local_upload_441_16_stereo (struct backend *backend, const gchar * path,
			    GByteArray * input, struct job_control *control)
{
  struct sample_info *sample_info_dst = control->data;
  sample_info_dst->rate = 44100;
  sample_info_dst->channels = 2;
  return sample_save_from_array (path, input, control);
}

static gint
local_upload_441_16_mono (struct backend *backend, const gchar * path,
			  GByteArray * input, struct job_control *control)
{
  struct sample_info *sample_info_dst = control->data;
  sample_info_dst->rate = 44100;
  sample_info_dst->channels = 1;
  return sample_save_from_array (path, input, control);
}

static gboolean
local_file_exists (struct backend *backend, const gchar * path)
{
  return access (path, F_OK) == 0;
}

const struct fs_operations FS_LOCAL_GENERIC_OPERATIONS = {
  .fs = 0,
  .options = FS_OPTION_SORT_BY_NAME,
  .name = "local",
  .gui_name = "localhost",
  .gui_icon = BE_FILE_ICON_GENERIC,
  .readdir = local_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .max_name_len = 255
};

const struct fs_operations FS_LOCAL_SAMPLE_OPERATIONS = {
  .fs = 0,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_ATTRS,
  .name = "local",
  .gui_name = "localhost",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_samples_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  //While the local operatinos do not neet to implement download, upload, load or save, the upload function is used by the editor to save the loaded sample in the appropriate format.
  .upload = local_upload,
  .get_ext = backend_get_fs_ext,
  .type_ext = "wav",
  .max_name_len = 255
};

enum local_fs
{
  FS_SAMPLES_LOCAL_48_16_STEREO = 0x1,
  FS_SAMPLES_LOCAL_48_16_MONO = 0x2,
  FS_SAMPLES_LOCAL_441_16_STEREO = 0x4,
  FS_SAMPLES_LOCAL_441_16_MONO = 0x8
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_48_16_STEREO,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_AUDIO_PLAYER |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "wav4816s",
  .gui_name = "WAV 48 KHz 16 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_samples_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload_48_16_stereo,
  .load = local_sample_load_48_16_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = local_get_upload_path,
  .get_download_path = local_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_48_16_MONO,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_AUDIO_PLAYER |
    FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "wav4816m",
  .gui_name = "WAV 48 KHz 16 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_samples_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload_48_16_mono,
  .load = local_sample_load_48_16_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = local_get_upload_path,
  .get_download_path = local_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_16_STEREO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_AUDIO_PLAYER |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "wav44116s",
  .gui_name = "WAV 44.1 KHz 16 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_samples_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload_441_16_stereo,
  .load = local_sample_load_441_16_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = local_get_upload_path,
  .get_download_path = local_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_16_MONO,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_AUDIO_PLAYER |
    FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "wav44116m",
  .gui_name = "WAV 44.1 KHz 16 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_samples_read_dir,
  .file_exists = local_file_exists,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = local_download,
  .upload = local_upload_441_16_mono,
  .load = local_sample_load_441_16_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = local_get_upload_path,
  .get_download_path = local_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

static const struct fs_operations *FS_SYSTEM_OPERATIONS[] = {
  &FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS,
  NULL
};

gint
system_init_backend (struct backend *backend, const gchar * id)
{
  if (strcmp (id, BE_SYSTEM_ID))
    {
      return -ENODEV;
    }

  backend->type = BE_TYPE_SYSTEM;
  backend->filesystems =
    FS_SAMPLES_LOCAL_48_16_STEREO | FS_SAMPLES_LOCAL_48_16_MONO |
    FS_SAMPLES_LOCAL_441_16_STEREO | FS_SAMPLES_LOCAL_441_16_MONO;
  backend->fs_ops = FS_SYSTEM_OPERATIONS;
  snprintf (backend->name, LABEL_MAX, "%s", _("System"));
  *backend->version = 0;
  *backend->description = 0;
  return 0;
}
