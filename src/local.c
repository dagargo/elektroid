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
#include <libgen.h>
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
  gchar *src_pathc = strdup (src_path);
  gchar *path = malloc (PATH_MAX);
  gchar *filename = basename (src_pathc);
  remove_ext (filename);
  snprintf (path, PATH_MAX, "%s/%s.wav", dst_dir, filename);
  g_free (src_pathc);
  return path;
}

static gchar *
local_get_upload_path (struct backend *backend,
		       const struct fs_operations *ops,
		       const gchar * dst_dir,
		       const gchar * src_path, gint32 * next_index)
{
  return local_get_download_path (backend, ops, dst_dir, src_path, NULL);
}

gint
local_mkdir (struct backend *backend, const gchar * name)
{
  DIR *dir;
  gint res = 0;
  gchar *dup;
  gchar *parent;

  dup = strdup (name);
  parent = dirname (dup);

  dir = opendir (parent);
  if (dir)
    {
      closedir (dir);
    }
  else
    {
      res = local_mkdir (backend, parent);
      if (res)
	{
	  goto cleanup;
	}
    }

  if (mkdir (name, 0755) == 0 || errno == EEXIST)
    {
      res = 0;
    }
  else
    {
      error_print ("Error while creating dir %s\n", name);
      res = -errno;
    }

cleanup:
  g_free (dup);
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
	  new_path = chain_path (path, dirent->d_name);
	  local_delete (backend, new_path);
	  free (new_path);
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
local_next_dentry (struct item_iterator *iter)
{
  gchar *full_path;
  struct dirent *dirent;
  gboolean found;
  struct stat st;
  mode_t mode;
  struct local_iterator_data *data = iter->data;

  while ((dirent = readdir (data->dir)) != NULL)
    {
      if (dirent->d_name[0] == '.')
	{
	  continue;
	}

      full_path = chain_path (data->path, dirent->d_name);
      if (stat (full_path, &st))
	{
	  free (full_path);
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
	  found = TRUE;
	  break;
	default:
	  error_print
	    ("stat mode neither file nor directory for %s\n", full_path);
	  found = FALSE;
	}

      free (full_path);

      if (found)
	{
	  return 0;
	}
    }

  return -ENOENT;
}

static gint
local_init_iterator (struct item_iterator *iter, const gchar * path,
		     gboolean cached)
{
  DIR *dir;
  struct local_iterator_data *data;

  if (!(dir = opendir (path)))
    {
      return -errno;
    }

  data = malloc (sizeof (struct local_iterator_data));
  if (!data)
    {
      closedir (dir);
      return -errno;
    }

  data->dir = dir;
  data->path = strdup (path);

  iter->data = data;
  iter->next = local_next_dentry;
  iter->free = local_free_iterator_data;

  return 0;
}

static gint
local_read_dir (struct backend *backend, struct item_iterator *iter,
		const gchar * path)
{
  return local_init_iterator (iter, path, FALSE);
}

static gint
local_sample_load_custom (const gchar * path, GByteArray * sample,
			  struct job_control *control,
			  const struct sample_params *sample_params)
{
  guint frames;
  control->parts = 1;
  control->part = 0;
  gint err = sample_load_from_file (path, sample, control, sample_params,
				    &frames);
  struct sample_info *sample_info = control->data;
  sample_info->samplerate = sample_params->samplerate;
  sample_info->channels =
    sample_info->channels <
    sample_params->channels ? sample_info->channels : sample_params->channels;
  return err;
}

static gint
local_sample_load_48_16_stereo (const gchar * path, GByteArray * sample,
				struct job_control *control)
{
  struct sample_params sample_params;
  sample_params.samplerate = 48000;
  sample_params.channels = 2;
  return local_sample_load_custom (path, sample, control, &sample_params);
}

static gint
local_sample_load_48_16_mono (const gchar * path, GByteArray * sample,
			      struct job_control *control)
{
  struct sample_params sample_params;
  sample_params.samplerate = 48000;
  sample_params.channels = 1;
  return local_sample_load_custom (path, sample, control, &sample_params);
}

static gint
local_upload (struct backend *backend, const gchar * path, GByteArray * input,
	      struct job_control *control)
{
  return sample_save_from_array (path, input, control);
}

static gint
local_sample_load_441_16_stereo (const gchar * path, GByteArray * sample,
				 struct job_control *control)
{
  struct sample_params sample_params;
  sample_params.samplerate = 44100;
  sample_params.channels = 2;
  return local_sample_load_custom (path, sample, control, &sample_params);
}

static gint
local_sample_load_441_16_mono (const gchar * path, GByteArray * sample,
			       struct job_control *control)
{
  struct sample_params sample_params;
  sample_params.samplerate = 44100;
  sample_params.channels = 1;
  return local_sample_load_custom (path, sample, control, &sample_params);
}

const struct fs_operations FS_LOCAL_OPERATIONS = {
  .fs = 0,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_AUDIO_PLAYER |
    FS_OPTION_STEREO,
  .name = "local",
  .gui_name = "localhost",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .get_ext = backend_get_fs_ext,
  .type_ext = "wav",
  .max_name_len = 255
};

enum sds_fs
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
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload,
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
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload,
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
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .download = local_download,
  .upload = local_upload,
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
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .rename = local_rename,
  .move = local_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = local_download,
  .upload = local_upload,
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
  return 0;
}
