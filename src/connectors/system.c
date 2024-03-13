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

#include <errno.h>
#include <glib/gi18n.h>
#if defined(__linux__)
#include <sys/statvfs.h>
#include <mntent.h>
#endif
#include "local.h"
#include "sample.h"
#include "connectors/common.h"
#include <sys/unistd.h>

struct system_iterator_data
{
  GDir *dir;
  gchar *path;
  const gchar **extensions;
};

static gint
system_download (struct backend *backend, const gchar *path,
		 GByteArray *output, struct job_control *control)
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
system_get_download_path (struct backend *backend,
			  const struct fs_operations *ops,
			  const gchar *dst_dir, const gchar *src_path,
			  GByteArray *content)
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
system_get_upload_path (struct backend *backend,
			const struct fs_operations *ops,
			const gchar *dst_dir, const gchar *src_path)
{
  return system_get_download_path (backend, ops, dst_dir, src_path, NULL);
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
      error_print ("Error while creating dir %s\n", name);
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
      debug_print (1, "Deleting local %s dir...\n", path);

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
      debug_print (1, "Deleting local %s file...\n", path);
      return unlink (path);
    }
}

gint
system_rename (struct backend *backend, const gchar *old, const gchar *new)
{
  debug_print (1, "Renaming locally from %s to %s...\n", old, new);
  return rename (old, new);
}

static void
system_free_iterator_data (void *iter_data)
{
  struct system_iterator_data *data = iter_data;
  g_dir_close (data->dir);
  g_free (data->path);
  g_free (data);
}

static gint
system_next_dentry (struct item_iterator *iter, gboolean sample_info)
{
  gchar *full_path;
  const gchar *name;
  struct stat st;
  struct system_iterator_data *data = iter->data;

  while ((name = g_dir_read_name (data->dir)) != NULL)
    {
      if (name[0] == '.')
	{
	  continue;
	}

      full_path = path_chain (PATH_SYSTEM, data->path, name);
      enum item_type type;
      if (g_file_test (full_path, G_FILE_TEST_IS_DIR))
	{
	  type = ELEKTROID_DIR;
	}
      else if (g_file_test (full_path, G_FILE_TEST_IS_REGULAR))
	{
	  type = ELEKTROID_FILE;
	}
      else
	{
	  error_print ("'%s' is neither file nor directory\n", full_path);
	  continue;
	}

      if (!stat (full_path, &st))
	{
	  snprintf (iter->item.name, LABEL_MAX, "%s", name);
	  iter->item.type = type;
	  iter->item.size = st.st_size;
	  iter->item.id = -1;

	  if (iter_is_dir_or_matches_extensions (iter, data->extensions))
	    {
	      if (iter->item.type == ELEKTROID_FILE && sample_info)
		{
		  sample_load_sample_info (full_path,
					   &iter->item.sample_info);
		}
	      g_free (full_path);
	      return 0;
	    }
	}

      g_free (full_path);
    }

  return -ENOENT;
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

static gint
system_read_dir_opts (struct backend *backend, struct item_iterator *iter,
		      const gchar *path, const gchar **extensions,
		      iterator_next next)
{
  GDir *dir;
  struct system_iterator_data *data;

  if (!(dir = g_dir_open (path, 0, NULL)))
    {
      return -errno;
    }

  data = g_malloc (sizeof (struct system_iterator_data));
  data->dir = dir;
  data->path = strdup (path);
  data->extensions = extensions;

  iter->data = data;
  iter->next = next;
  iter->free = system_free_iterator_data;

  return 0;
}

gint
system_read_dir (struct backend *backend, struct item_iterator *iter,
		 const gchar *path, const gchar **extensions)
{
  return system_read_dir_opts (backend, iter, path, extensions,
			       system_next_dentry_without_sample_info);
}

gint
system_samples_read_dir (struct backend *backend, struct item_iterator *iter,
			 const gchar *path, const gchar **extensions)
{
  return system_read_dir_opts (backend, iter, path,
			       sample_get_sample_extensions (),
			       system_next_dentry_with_sample_info);
}

static gint
system_load_custom (const gchar *path, GByteArray *sample,
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
system_load_48_16_stereo (const gchar *path, GByteArray *sample,
			  struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 48000;
  sample_info_dst.channels = 2;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

gint
system_upload (struct backend *backend, const gchar *path,
	       GByteArray *input, struct job_control *control)
{
  return sample_save_to_file (path, input, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static gint
system_load_48_16_mono (const gchar *path, GByteArray *sample,
			struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 48000;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_16_stereo (const gchar *path, GByteArray *sample,
			   struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 2;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_16_mono (const gchar *path, GByteArray *sample,
			 struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_24_stereo (const gchar *path, GByteArray *sample,
			   struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 2;
  sample_info_dst.format = SF_FORMAT_PCM_32;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_upload_24_bits (struct backend *backend, const gchar *path,
		       GByteArray *input, struct job_control *control)
{
  return sample_save_to_file (path, input, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_24);
}

static gint
system_load_441_24_mono (const gchar *path, GByteArray *sample,
			 struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_32;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_8_stereo (const gchar *path, GByteArray *sample,
			  struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 2;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_upload_8_bits (struct backend *backend, const gchar *path,
		      GByteArray *input, struct job_control *control)
{
  return sample_save_to_file (path, input, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_U8);
}

static gint
system_load_441_8_mono (const gchar *path, GByteArray *sample,
			struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_32_16_mono (const gchar *path, GByteArray *sample,
			struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 32000;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

gboolean
system_file_exists (struct backend *backend, const gchar *path)
{
  return access (path, F_OK) == 0;
}

enum system_fs
{
  FS_SAMPLES_LOCAL_48_16_STEREO = 0x1,
  FS_SAMPLES_LOCAL_48_16_MONO = 0x2,
  FS_SAMPLES_LOCAL_441_16_STEREO = 0x4,
  FS_SAMPLES_LOCAL_441_16_MONO = 0x8,
  FS_SAMPLES_LOCAL_441_24_STEREO = 0x10,
  FS_SAMPLES_LOCAL_441_24_MONO = 0x20,
  FS_SAMPLES_LOCAL_441_8_STEREO = 0x40,
  FS_SAMPLES_LOCAL_441_8_MONO = 0x80,
  FS_SAMPLES_LOCAL_32_16_MONO = 0x100,
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_48_16_STEREO,
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav48k16b2c",
  .gui_name = "WAV 48 KHz 16 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_48_16_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_48_16_MONO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav48k16b1c",
  .gui_name = "WAV 48 KHz 16 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_48_16_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_16_STEREO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k16b2c",
  .gui_name = "WAV 44.1 KHz 16 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_441_16_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_16_MONO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k16b1c",
  .gui_name = "WAV 44.1 KHz 16 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .print_item = common_print_item,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_441_16_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_24_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_24_STEREO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k24b2c",
  .gui_name = "WAV 44.1 KHz 24 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_24_bits,
  .load = system_load_441_24_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_24_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_24_MONO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k24b1c",
  .gui_name = "WAV 44.1 KHz 24 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = system_download,
  .upload = system_upload_24_bits,
  .load = system_load_441_24_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_8_STEREO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_8_STEREO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR |
    FS_OPTION_STEREO | FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k8b2c",
  .gui_name = "WAV 44.1 KHz 8 bits stereo",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_441_8_stereo,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_8_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_441_8_MONO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k8b1c",
  .gui_name = "WAV 44.1 KHz 8 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_441_8_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_32_16_MONO_OPERATIONS = {
  .fs = FS_SAMPLES_LOCAL_32_16_MONO,
  .options =
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "wav32k8b1c",
  .gui_name = "WAV 32 KHz 16 bits mono",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = system_download,
  .upload = system_upload,
  .load = system_load_32_16_mono,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = system_get_upload_path,
  .get_download_path = system_get_download_path,
  .type_ext = "wav",
  .max_name_len = 255
};

static const struct fs_operations *FS_SYSTEM_OPERATIONS[] = {
  &FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_24_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_24_MONO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_8_STEREO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_441_8_MONO_OPERATIONS,
  &FS_SYSTEM_SAMPLES_32_16_MONO_OPERATIONS,
  NULL
};

#if defined (__linux__)
static gint
system_get_storage_stats (struct backend *backend, gint type,
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

gint
system_init_backend (struct backend *backend, const gchar *id)
{
  if (strcmp (id, BE_SYSTEM_ID))
    {
      return -ENODEV;
    }

  backend->type = BE_TYPE_SYSTEM;
  backend->filesystems =
    FS_SAMPLES_LOCAL_48_16_STEREO | FS_SAMPLES_LOCAL_48_16_MONO |
    FS_SAMPLES_LOCAL_441_16_STEREO | FS_SAMPLES_LOCAL_441_16_MONO |
    FS_SAMPLES_LOCAL_441_24_STEREO | FS_SAMPLES_LOCAL_441_24_MONO |
    FS_SAMPLES_LOCAL_441_8_STEREO | FS_SAMPLES_LOCAL_441_8_MONO |
    FS_SAMPLES_LOCAL_32_16_MONO;
  backend->fs_ops = FS_SYSTEM_OPERATIONS;
  snprintf (backend->name, LABEL_MAX, "%s", _("System"));
  *backend->version = 0;
  *backend->description = 0;
  backend->conn_name = "system";
  backend->destroy_data = NULL;
  backend->upgrade_os = NULL;
#if defined (__linux__)
  backend->get_storage_stats = system_get_storage_stats;
  backend->storage = 1;
#else
  backend->get_storage_stats = NULL;
  backend->storage = 0;
#endif
  return 0;
}
