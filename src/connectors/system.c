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
		 struct idata *idata, struct job_control *control)
{
  gint err;

  job_control_reset (control, 1);

  err = file_load (path, idata, control);

  if (!err)
    {
      job_control_set_progress (control, 1.0);
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

      if (!stat (full_path, &st))
	{
	  snprintf (iter->item.name, LABEL_MAX, "%s", name);
	  iter->item.type = type;
	  iter->item.size = st.st_size;
	  iter->item.id = -1;

	  if (item_iterator_is_dir_or_matches_extensions (iter,
							  data->extensions))
	    {
	      if (iter->item.type == ITEM_TYPE_FILE && sample_info)
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
  snprintf (iter->item.name, LABEL_MAX, "%s", name);
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
		    struct job_control *control,
		    const struct sample_info *sample_info_req)
{
  struct sample_info sample_info_src;
  //Typically, control parts are set not here but in this case makes more sense.
  control->parts = 1;
  control->part = 0;
  return sample_load_from_file (path, sample, control, sample_info_req,
				&sample_info_src);
}

static gint
system_load_48_16_stereo (const gchar *path, struct idata *sample,
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
	       struct idata *sample, struct job_control *control)
{
  //Typically, control parts are set here but in this case makes more sense do it in the load functions.
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static gint
system_load_48_16_mono (const gchar *path, struct idata *sample,
			struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 48000;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_16_stereo (const gchar *path, struct idata *sample,
			   struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 2;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_16_mono (const gchar *path, struct idata *sample,
			 struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_24_stereo (const gchar *path, struct idata *sample,
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
		       struct idata *sample, struct job_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_24);
}

static gint
system_load_441_24_mono (const gchar *path, struct idata *sample,
			 struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_32;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_441_8_stereo (const gchar *path, struct idata *sample,
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
		      struct idata *sample, struct job_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_U8);
}

static gint
system_load_441_8_mono (const gchar *path, struct idata *sample,
			struct job_control *control)
{
  struct sample_info sample_info_dst;
  sample_info_dst.rate = 44100;
  sample_info_dst.channels = 1;
  sample_info_dst.format = SF_FORMAT_PCM_16;
  return system_load_custom (path, sample, control, &sample_info_dst);
}

static gint
system_load_32_16_mono (const gchar *path, struct idata *sample,
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
  FS_SAMPLES_LOCAL_48_16_STEREO,
  FS_SAMPLES_LOCAL_48_16_MONO,
  FS_SAMPLES_LOCAL_441_16_STEREO,
  FS_SAMPLES_LOCAL_441_16_MONO,
  FS_SAMPLES_LOCAL_441_24_STEREO,
  FS_SAMPLES_LOCAL_441_24_MONO,
  FS_SAMPLES_LOCAL_441_8_STEREO,
  FS_SAMPLES_LOCAL_441_8_MONO,
  FS_SAMPLES_LOCAL_32_16_MONO
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_48_16_STEREO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav48k16b2c",
  .gui_name = "WAV 48 KHz 16 bits stereo",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_48_16_MONO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav48k16b1c",
  .gui_name = "WAV 48 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_16_STEREO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k16b2c",
  .gui_name = "WAV 44.1 KHz 16 bits stereo",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_16_MONO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k16b1c",
  .gui_name = "WAV 44.1 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_24_STEREO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_24_STEREO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k24b2c",
  .gui_name = "WAV 44.1 KHz 24 bits stereo",
  .gui_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_24_bits,
  .load = system_load_441_24_stereo,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_24_MONO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_24_MONO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k24b1c",
  .gui_name = "WAV 44.1 KHz 24 bits mono",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_8_STEREO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_8_STEREO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_STEREO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k8b2c",
  .gui_name = "WAV 44.1 KHz 8 bits stereo",
  .gui_icon = FS_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .download = system_download,
  .upload = system_upload_8_bits,
  .load = system_load_441_8_stereo,
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_441_8_MONO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_441_8_MONO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav44.1k8b1c",
  .gui_name = "WAV 44.1 KHz 8 bits mono",
  .gui_icon = FS_ICON_WAVE,
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
  .save = file_save,
  .get_upload_path = common_system_get_upload_path,
  .get_download_path = common_system_get_download_path,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};

const struct fs_operations FS_SYSTEM_SAMPLES_32_16_MONO_OPERATIONS = {
  .id = FS_SAMPLES_LOCAL_32_16_MONO,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "wav32k8b1c",
  .gui_name = "WAV 32 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
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
  gslist_fill (&backend->fs_ops, &FS_SYSTEM_SAMPLES_48_16_STEREO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_48_16_MONO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_16_STEREO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_16_MONO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_24_STEREO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_24_MONO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_8_STEREO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_441_8_MONO_OPERATIONS,
	       &FS_SYSTEM_SAMPLES_32_16_MONO_OPERATIONS, NULL);
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
  .standard = FALSE,
  .regex = NULL
};
