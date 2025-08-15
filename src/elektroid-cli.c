/*
 *   elektroid-cli.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
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

#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__linux__)
#include <signal.h>
#endif
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "backend.h"
#include "regconn.h"
#include "regpref.h"
#include "utils.h"

#define CLI_SLEEP_US 200000

#define COMMAND_NOT_IN_SYSTEM_FS "Command not available in system backend"

#define GET_FS_OPS_OFFSET(member) offsetof(struct fs_operations, member)
#define GET_FS_OPS_FUNC(type,fs,offset) (*(((type *) (((gchar *) fs) + offset))))
#define RETURN_IF_NULL(f) if (!(f)) {return -ENOSYS;}

static struct backend backend;
static struct job_control job_control;
static struct controllable controllable;	//Used for CLI control for operations that do not use job_control or sysex_transfer.
static gchar *connector, *fs, *op;

const struct fs_operations *fs_ops;
const gchar *current_path_progress;
gboolean same_line_progress;

static void
complete_progress (gint err)
{
  if (same_line_progress && !err)
    {
      fprintf (stderr, "\n");
    }
}

static void
print_progress (struct job_control *job_control)
{
  gint progress = job_control->progress * 100;
  const gchar *end = same_line_progress ? "\r" : "\n";
  fprintf (stderr, "%s: %3d %%%s", current_path_progress, progress, end);
  if (same_line_progress)
    {
      fflush (stderr);
    }
}

static void
set_progress_type ()
{
  same_line_progress = !debug_level && isatty (fileno (stderr));
}

static const gchar *
cli_get_path (const gchar *device_path)
{
  gint len = strlen (device_path);
  const gchar *path = device_path;
  gint i = 0;

  while (*path != G_SEARCHPATH_SEPARATOR && i < len)
    {
      path++;
      i++;
    }
  path++;

  return path;
}

static gint
cli_ld ()
{
  struct backend_device device;
  GArray *devices = backend_get_devices ();

  for (gint i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct backend_device, i);
      printf ("%d: id: %s; name: %s\n", i, device.id, device.name);
    }

  g_array_free (devices, TRUE);

  return EXIT_SUCCESS;
}

static gint
cli_connect (const gchar *device_path)
{
  struct backend_device device;
  GArray *devices = backend_get_devices ();
  gint err, id;
  gchar *rem;

  if (!devices->len)
    {
      error_print ("No devices found");
      err = -ENODEV;
      goto end;
    }

  errno = 0;
  id = (gint) g_ascii_strtoll (device_path, &rem, 10);
  if (errno || device_path == rem)
    {
      error_print ("Device not provided properly in '%s'", device_path);
      err = -ENODEV;
      goto end;
    }

  if (id >= devices->len)
    {
      error_print ("Invalid device '%d'", id);
      err = -ENODEV;
      goto end;
    }

  device = g_array_index (devices, struct backend_device, id);
  err = backend_init_connector (&backend, &device, connector, NULL);

  if (!err && fs)
    {
      fs_ops = backend_get_fs_operations_by_name (&backend, fs);
      if (!fs_ops)
	{
	  error_print ("Invalid filesystem '%s'", fs);
	  err = -EINVAL;
	}
    }

end:
  g_array_free (devices, TRUE);
  return err;
}

static gint
cli_list (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *path;
  struct item_iterator iter;
  const gchar *device_path;

  if (*optind == argc)
    {
      error_print ("Remote path missing");
      return -EINVAL;
    }
  else
    {
      device_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_path);
  if (err)
    {
      return err;
    }

  RETURN_IF_NULL (fs_ops->readdir);
  RETURN_IF_NULL (fs_ops->print_item);

  path = cli_get_path (device_path);
  err = fs_ops->readdir (&backend, &iter, path,
			 fs_ops->get_exts (&backend, fs_ops));
  if (err)
    {
      return err;
    }

  while (!item_iterator_next (&iter) &&
	 controllable_is_active (&controllable))
    {
      fs_ops->print_item (&iter, &backend, fs_ops);
    }

  item_iterator_free (&iter);

  return controllable_is_active (&controllable) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static gint
cli_command_path (int argc, gchar *argv[], int *optind, ssize_t member_offset)
{
  const gchar *path;
  const gchar *device_path;
  gint err;
  fs_path_func f;

  if (*optind == argc)
    {
      error_print ("Remote path missing");
      return -EINVAL;
    }
  else
    {
      device_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_path);
  if (err)
    {
      return err;
    }

  f = GET_FS_OPS_FUNC (fs_path_func, fs_ops, member_offset);
  RETURN_IF_NULL (f);

  path = cli_get_path (device_path);
  return f (&backend, path);
}

static gint
cli_command_src_dst (int argc, gchar *argv[], int *optind,
		     ssize_t member_offset)
{
  const gchar *src_path, *dst_path;
  gchar *device_src_path, *device_dst_path;
  gint src_card, dst_card, err;
  fs_src_dst_func f;

  if (*optind == argc)
    {
      error_print ("Remote path source missing");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path destination missing");
      return -EINVAL;
    }
  else
    {
      device_dst_path = argv[*optind];
      (*optind)++;
    }

  src_card = atoi (device_src_path);
  dst_card = atoi (device_dst_path);
  if (src_card != dst_card)
    {
      error_print ("Source and destination device must be the same");
      return -EINVAL;
    }

  err = cli_connect (device_src_path);
  if (err)
    {
      return err;
    }

  f = GET_FS_OPS_FUNC (fs_src_dst_func, fs_ops, member_offset);
  RETURN_IF_NULL (f);

  src_path = cli_get_path (device_src_path);
  dst_path = cli_get_path (device_dst_path);
  return f (&backend, src_path, dst_path);
}

static gint
cli_command_mv_rename (int argc, gchar *argv[], int *optind)
{
  const gchar *src_path, *dst_path;
  gchar *device_src_path, *device_dst_path;
  gint src_card, dst_card, err;
  fs_src_dst_func f;

  if (*optind == argc)
    {
      error_print ("Remote path source missing");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path destination missing");
      return -EINVAL;
    }
  else
    {
      device_dst_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_src_path);
  if (err)
    {
      return err;
    }

  src_card = atoi (device_src_path);
  src_path = cli_get_path (device_src_path);

  f = fs_ops->move;
  // If move is implemented, rename must behave the same way.
  if (f)
    {
      dst_card = atoi (device_dst_path);
      if (src_card != dst_card)
	{
	  error_print ("Source and destination device must be the same");
	  return -EINVAL;
	}
      dst_path = cli_get_path (device_dst_path);
    }
  else
    {
      f = fs_ops->rename;
      RETURN_IF_NULL (f);

      dst_path = device_dst_path;
    }

  return f (&backend, src_path, dst_path);
}

static const gchar *
cli_get_backend_type_name ()
{
  switch (backend.type)
    {
    case BE_TYPE_SYSTEM:
      return "SYSTEM";
    case BE_TYPE_MIDI:
      return "MIDI";
    case BE_TYPE_NO_MIDI:
      return "NO-MIDI";
    default:
      return "UNKNOWN";
    }
}

static gint
cli_fs_compare (gconstpointer a, gconstpointer b)
{
  const struct fs_operations *fsa = a;
  const struct fs_operations *fsb = b;
  return strcmp (fsa->name, fsb->name);
}

static gint
cli_info (int argc, gchar *argv[], int *optind)
{
  const gchar *device_path;
  gint err;
  gboolean first;
  GSList *e, *sorted;

  if (*optind == argc)
    {
      error_print ("Device missing");
      return -EINVAL;
    }
  else
    {
      device_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_path);
  if (err)
    {
      return err;
    }

  printf ("Type: %s\n", cli_get_backend_type_name ());
  printf ("Device name: %s\n", backend.name);
  printf ("Device version: %s\n", backend.version);
  printf ("Device description: %s\n", backend.description);
  printf ("Connector name: %s\n", backend.conn_name);
  printf ("Filesystems: ");

  sorted = g_slist_copy (backend.fs_ops);
  sorted = g_slist_sort (sorted, cli_fs_compare);

  e = sorted;
  first = TRUE;
  while (e)
    {
      const struct fs_operations *fs_ops = e->data;
      gboolean cli_only = fs_ops->gui_name == NULL;
      printf ("%s%s%s", first ? "" : ", ", fs_ops->name,
	      cli_only ? " (CLI only)" : "");
      first = FALSE;
      e = e->next;
    }
  printf ("\n");

  g_slist_free (g_steal_pointer (&sorted));

  return EXIT_SUCCESS;
}

static gint
cli_df (int argc, gchar *argv[], int *optind)
{
  const gchar *device_path;
  const gchar *path;
  gchar *size;
  gchar *diff;
  gchar *free;
  gint err;
  struct backend_storage_stats statfs;

  if (*optind == argc)
    {
      error_print ("Device missing");
      return -EINVAL;
    }
  else
    {
      device_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_path);
  if (err)
    {
      return err;
    }

  if (!backend.get_storage_stats)
    {
      return -ENOSYS;
    }

  path = cli_get_path (device_path);
  if (!strlen (path))
    {
      return -EINVAL;
    }

  printf ("%-20.20s%16.16s%16.16s%16.16s%11.10s\n", "Storage", "Size",
	  "Used", "Available", "Use%");

  err = 0;
  for (guint i = 1; i < G_MAXUINT8; i <<= 1)
    {
      gint v = backend.get_storage_stats (&backend, i, &statfs, path);
      if (v >= 0)
	{
	  size = get_human_size (statfs.bsize, FALSE);
	  diff = get_human_size (statfs.bsize - statfs.bfree, FALSE);
	  free = get_human_size (statfs.bfree, FALSE);
	  printf ("%-20.20s%16s%16s%16s%10.2f%%\n",
		  statfs.name, size, diff, free,
		  backend_get_storage_stats_percent (&statfs));
	  g_free (size);
	  g_free (diff);
	  g_free (free);
	}

      if (!v)
	{
	  break;
	}
    }

  return err;
}

static gint
cli_upgrade_os (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *src_path;
  const gchar *device_path;
  struct idata idata;
  struct sysex_transfer sysex_transfer;

  if (*optind == argc)
    {
      error_print ("Local path missing");
      return EXIT_FAILURE;
    }
  else
    {
      src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path missing");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_path);
  if (err)
    {
      return err;
    }
  if (backend.type == BE_TYPE_SYSTEM)
    {
      error_print (COMMAND_NOT_IN_SYSTEM_FS);
      return EXIT_FAILURE;
    }

  RETURN_IF_NULL (backend.upgrade_os);

  err = file_load (src_path, &idata, NULL);
  if (err)
    {
      error_print ("Error while loading '%s'.", src_path);
    }
  else
    {
      sysex_transfer_init_tx (&sysex_transfer, idata_steal (&idata));
      err = backend.upgrade_os (&backend, &sysex_transfer, &controllable);
      sysex_transfer_free (&sysex_transfer);
    }

  return err;
}

static gint
cli_download_item (const gchar *src_path, const gchar *dst_path)
{
  gint err;
  gchar *download_path;
  struct idata idata;

  RETURN_IF_NULL (fs_ops->download);
  RETURN_IF_NULL (fs_ops->get_download_path);
  RETURN_IF_NULL (fs_ops->save);

  controllable_set_active (&job_control.controllable, TRUE);
  job_control.callback = print_progress;
  current_path_progress = src_path;

  err = fs_ops->download (&backend, src_path, &idata, &job_control);
  if (err)
    {
      return err;
    }

  download_path = fs_ops->get_download_path (&backend, fs_ops, dst_path,
					     src_path, &idata);
  if (!download_path)
    {
      err = -EINVAL;
      goto cleanup;
    }

  err = fs_ops->save (download_path, &idata, &job_control);
  g_free (download_path);

cleanup:
  idata_free (&idata);

  complete_progress (err);

  return err;
}

static gint
cli_download_dir (const gchar *src_path, const gchar *dst_path)
{
  gint err;
  struct item_iterator iter;

  RETURN_IF_NULL (fs_ops->readdir);

  err = fs_ops->readdir (&backend, &iter, src_path, NULL);
  if (err)
    {
      return err;
    }

  while (!item_iterator_next (&iter) && controllable_is_active (&controllable)
	 && !err)
    {
      gchar *rsrc_path;
      gchar *filename = item_get_filename (&iter.item, fs_ops->options);
      rsrc_path = path_chain (PATH_INTERNAL, src_path, filename);
      g_free (filename);

      if (iter.item.type == ITEM_TYPE_FILE && iter.item.size != 0)	//File and non empty slot
	{
	  err = cli_download_item (rsrc_path, dst_path);
	}
      else if (iter.item.type == ITEM_TYPE_DIR)
	{
	  gchar *rdst_path = path_chain (PATH_SYSTEM, dst_path,
					 iter.item.name);

	  err = g_mkdir (rdst_path, 0755);
	  if (err)
	    {
	      error_print
		("Error while creating directory '%s'. Continuing...",
		 rdst_path);
	    }
	  else
	    {
	      err = cli_download_dir (rsrc_path, rdst_path);
	    }

	  g_free (rdst_path);
	}

      g_free (rsrc_path);
    }

  item_iterator_free (&iter);

  return controllable_is_active (&controllable) ? err : -ECANCELED;
}

static gint
cli_download (int argc, gchar *argv[], int *optind, gint recursive)
{
  const gchar *src_path;
  const gchar *dst_path;
  gchar *device_src_path;
  gint err;

  if (*optind == argc)
    {
      error_print ("Remote path missing");
      return EXIT_FAILURE;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_src_path);
  if (err)
    {
      return err;
    }

  src_path = cli_get_path (device_src_path);

  if (*optind == argc)
    {
      dst_path = ".";
    }
  else
    {
      dst_path = argv[*optind];

      debug_print (1, "Creating directory '%s'...", dst_path);
      err = g_mkdir_with_parents (dst_path, 0755);
      if (err)
	{
	  error_print ("Error while creating directory '%s'", dst_path);
	  return err;
	}
    }

  if (recursive)
    {
      if (strcmp (src_path, "/"))
	{
	  gchar *new_dir = g_path_get_basename (src_path);
	  gchar *full_dst_path = path_chain (PATH_SYSTEM, dst_path, new_dir);
	  debug_print (1, "Creating directory '%s'...", full_dst_path);
	  err = g_mkdir (full_dst_path, 0755);
	  if (err)
	    {
	      error_print ("Error while creating directory '%s'",
			   full_dst_path);
	    }
	  else
	    {
	      err = cli_download_dir (src_path, full_dst_path);
	    }

	  g_free (full_dst_path);
	  g_free (new_dir);
	  return err;
	}
      else
	{
	  return cli_download_dir (src_path, dst_path);
	}
    }
  else
    {
      return cli_download_item (src_path, dst_path);
    }
}

static gint
cli_upload_item (const gchar *src_path, const gchar *dst_path)
{
  gint err;
  gchar *upload_path;
  struct idata idata;

  RETURN_IF_NULL (fs_ops->load);
  RETURN_IF_NULL (fs_ops->get_upload_path);
  RETURN_IF_NULL (fs_ops->upload);

  controllable_set_active (&job_control.controllable, TRUE);
  job_control.callback = print_progress;
  current_path_progress = src_path;

  err = fs_ops->load (src_path, &idata, &job_control);
  if (err)
    {
      return err;
    }

  upload_path = fs_ops->get_upload_path (&backend, fs_ops, dst_path,
					 src_path, &idata);

  err = fs_ops->upload (&backend, upload_path, &idata, &job_control);
  idata_free (&idata);

  g_free (upload_path);

  complete_progress (err);

  return err;
}

static gint
cli_upload (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *dst_path;
  gchar *src_path, *device_dst_path;

  if (*optind == argc)
    {
      error_print ("Local path missing");
      return EXIT_FAILURE;
    }
  else
    {
      src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path missing");
      return EXIT_FAILURE;
    }
  else
    {
      device_dst_path = argv[*optind];
      (*optind)++;
    }

  err = cli_connect (device_dst_path);
  if (err)
    {
      return err;
    }

  dst_path = cli_get_path (device_dst_path);

  return cli_upload_item (src_path, dst_path);
}

static gint
cli_send (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *device_dst_path, *src_file;
  struct idata idata;
  struct sysex_transfer sysex_transfer;

  if (*optind == argc)
    {
      error_print ("Source file missing");
      return -EINVAL;
    }
  else
    {
      src_file = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote device missing");
      return -EINVAL;
    }
  else
    {
      device_dst_path = argv[*optind];
      (*optind)++;
    }

  connector = "default";
  err = cli_connect (device_dst_path);
  if (err)
    {
      return err;
    }
  if (backend.type == BE_TYPE_SYSTEM)
    {
      error_print (COMMAND_NOT_IN_SYSTEM_FS);
      return EXIT_FAILURE;
    }

  err = file_load (src_file, &idata, NULL);
  if (err)
    {
      error_print ("Error while loading '%s'.", src_file);
    }
  else
    {
      sysex_transfer_init_tx (&sysex_transfer, idata_steal (&idata));
      err = backend_tx_sysex (&backend, &sysex_transfer, &controllable);
      sysex_transfer_free (&sysex_transfer);
    }

  return err;
}

static gint
cli_receive (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *device_src_path, *dst_file;
  struct idata idata;
  struct sysex_transfer sysex_transfer;

  if (*optind == argc)
    {
      error_print ("Remote device missing");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Destination file missing");
      return -EINVAL;
    }
  else
    {
      dst_file = argv[*optind];
      (*optind)++;
    }

  connector = "default";
  err = cli_connect (device_src_path);
  if (err)
    {
      return err;
    }
  if (backend.type == BE_TYPE_SYSTEM)
    {
      error_print (COMMAND_NOT_IN_SYSTEM_FS);
      return EXIT_FAILURE;
    }

  backend_rx_drain (&backend);

  //This doesn't need to be synchronized because the CLI is not multithreaded.
  sysex_transfer_init_rx (&sysex_transfer, BE_SYSEX_TIMEOUT_MS, TRUE);
  err = backend_rx_sysex (&backend, &sysex_transfer, &controllable);
  if (err)
    {
      error_print ("Error while downloading.");
    }
  else
    {
      idata_init (&idata, sysex_transfer_steal (&sysex_transfer), NULL, NULL);
      err = file_save (dst_file, &idata, NULL);
      idata_free (&idata);
    }

  return err;
}

#if defined(__linux__)
static void
cli_end (int sig)
{
  controllable_set_active (&controllable, FALSE);
  controllable_set_active (&job_control.controllable, FALSE);
}
#endif

int
main (int argc, gchar *argv[])
{
  gint c;
  gint err;
  gchar *command;
  gint vflg = 0, errflg = 0;

  controllable_init (&controllable);
  controllable_init (&job_control.controllable);

#if defined(__linux__)
  struct sigaction action;

  action.sa_handler = cli_end;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGHUP, &action, NULL);
#endif

  while ((c = getopt (argc, argv, "v")) != -1)
    {
      switch (c)
	{
	case 'v':
	  vflg++;
	  break;
	case '?':
	  errflg++;
	}
    }

  if (optind == argc)
    {
      errflg = 1;
    }
  else
    {
      command = argv[optind];
      optind++;
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (errflg > 0)
    {
      fprintf (stderr, "%s\n", PACKAGE_STRING);
      gchar *exec_name = g_path_get_basename (argv[0]);
      fprintf (stderr, "Usage: %s [options] command\n", exec_name);
      exit (EXIT_FAILURE);
    }

  set_progress_type ();

  regconn_register ();
  regpref_register ();
  preferences_load ();
  preferences_set_boolean (PREF_KEY_MIX, FALSE);	//This might be required by devices using the audio link.

  audio_init_and_wait ();

  if (!strcmp (command, "ld") || !strcmp (command, "list-devices"))
    {
      err = cli_ld ();
    }
  else if (!strcmp (command, "info") || !strcmp (command, "info-device"))
    {
      err = cli_info (argc, argv, &optind);
    }
  else if (!strcmp (command, "df") || !strcmp (command, "info-storage"))
    {
      err = cli_df (argc, argv, &optind);
    }
  else if (!strcmp (command, "send"))
    {
      err = cli_send (argc, argv, &optind);
    }
  else if (!strcmp (command, "receive"))
    {
      err = cli_receive (argc, argv, &optind);
    }
  else if (!strcmp (command, "upgrade"))
    {
      err = cli_upgrade_os (argc, argv, &optind);
    }
  else
    {
      err = command_set_parts (command, &connector, &fs, &op);
      if (err)
	{
	  goto end;
	}

      debug_print (1,
		   "Connector: \"%s\"; filesystem: \"%s\"; operation: \"%s\"",
		   connector, fs, op);

      if (!strcmp (op, "ls") || !strcmp (op, "list"))
	{
	  err = cli_list (argc, argv, &optind);
	}
      else if (!strcmp (op, "mkdir"))
	{
	  err = cli_command_path (argc, argv, &optind,
				  GET_FS_OPS_OFFSET (mkdir));
	}
      else if (!strcmp (op, "rm") || !strcmp (op, "rmdir"))
	{
	  err = cli_command_path (argc, argv, &optind,
				  GET_FS_OPS_OFFSET (delete));
	}
      else if (!strcmp (op, "download") || !strcmp (op, "dl"))
	{
	  err = cli_download (argc, argv, &optind, 0);
	}
      else if (!strcmp (op, "rdownload") || !strcmp (op, "rdl") ||
	       !strcmp (op, "backup"))
	{
	  err = cli_download (argc, argv, &optind, 1);
	}
      else if (!strcmp (op, "upload") || !strcmp (op, "ul"))
	{
	  err = cli_upload (argc, argv, &optind);
	}
      else if (!strcmp (op, "cl"))
	{
	  err = cli_command_path (argc, argv, &optind,
				  GET_FS_OPS_OFFSET (clear));
	}
      else if (!strcmp (op, "cp"))
	{
	  err = cli_command_src_dst (argc, argv, &optind,
				     GET_FS_OPS_OFFSET (copy));
	}
      else if (!strcmp (op, "sw"))
	{
	  err = cli_command_src_dst (argc, argv, &optind,
				     GET_FS_OPS_OFFSET (swap));
	}
      else if (!strcmp (op, "mv"))
	{
	  err = cli_command_mv_rename (argc, argv, &optind);
	}
      else
	{
	  error_print ("Command '%s' not recognized", command);
	  err = EXIT_FAILURE;
	}

      if (backend_check (&backend))
	{
	  backend_destroy (&backend);
	}

      g_free (connector);
      g_free (fs);
      g_free (op);
    }

end:
  if (err && err != EXIT_FAILURE)
    {
      error_print ("Error: %s", g_strerror (-err));
    }

  controllable_clear (&controllable);

  audio_destroy ();

  regconn_unregister ();
  regpref_unregister ();

  usleep (BE_REST_TIME_US * 2);
  return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
