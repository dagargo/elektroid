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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__linux__)
#include <signal.h>
#endif
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include "backend.h"
#include "connector.h"
#include "utils.h"

#define COMMAND_NOT_IN_SYSTEM_FS "Command not available in system backend\n"

#define GET_FS_OPS_OFFSET(member) offsetof(struct fs_operations, member)
#define GET_FS_OPS_FUNC(type,fs,offset) (*(((type *) (((gchar *) fs) + offset))))
#define CHECK_FS_OPS_FUNC(f) if (!(f)) {return -ENOSYS;}

static struct backend backend;
static struct job_control control;
static struct sysex_transfer sysex_transfer;
static gchar *connector, *fs, *op;
const struct fs_operations *fs_ops;

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
  gint i;
  struct backend_device device;
  GArray *devices = backend_get_devices ();

  for (i = 0; i < devices->len; i++)
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
  gint err, id = (gint) atoi (device_path);
  struct backend_device device;
  GArray *devices = backend_get_devices ();

  if (!devices->len || id >= devices->len)
    {
      error_print ("Invalid device %d\n", id);
      return -ENODEV;
    }

  device = g_array_index (devices, struct backend_device, id);
  err = connector_init_backend (&backend, &device, connector, NULL);
  g_array_free (devices, TRUE);

  if (!err && fs)
    {
      fs_ops = backend_get_fs_operations_by_name (&backend, fs);
      if (!fs_ops)
	{
	  error_print ("Invalid filesystem '%s'\n", fs);
	  return -EINVAL;
	}
    }

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
      error_print ("Remote path missing\n");
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

  path = cli_get_path (device_path);

  CHECK_FS_OPS_FUNC (fs_ops->readdir);

  err = fs_ops->readdir (&backend, &iter, path, NULL);
  if (err)
    {
      return err;
    }

  while (!next_item_iterator (&iter))
    {
      fs_ops->print_item (&iter, &backend, fs_ops);
    }

  free_item_iterator (&iter);

  return EXIT_SUCCESS;
}

static int
cli_command_path (int argc, gchar *argv[], int *optind, ssize_t member_offset)
{
  const gchar *path;
  const gchar *device_path;
  gint err;
  fs_path_func f;

  if (*optind == argc)
    {
      error_print ("Remote path missing\n");
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

  path = cli_get_path (device_path);

  f = GET_FS_OPS_FUNC (fs_path_func, fs_ops, member_offset);
  CHECK_FS_OPS_FUNC (f);
  err = f (&backend, path);
  return err;
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
      error_print ("Remote path source missing\n");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path destination missing\n");
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
      error_print ("Source and destination device must be the same\n");
      return -EINVAL;
    }

  err = cli_connect (device_src_path);
  if (err)
    {
      return err;
    }

  f = GET_FS_OPS_FUNC (fs_src_dst_func, fs_ops, member_offset);
  CHECK_FS_OPS_FUNC (f);
  src_path = cli_get_path (device_src_path);
  dst_path = cli_get_path (device_dst_path);
  err = f (&backend, src_path, dst_path);
  return err;
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
      error_print ("Remote path source missing\n");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path destination missing\n");
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

  // If move is implemented, rename must behave the same way.
  f = fs_ops->move;
  if (f)
    {
      dst_card = atoi (device_dst_path);
      if (src_card != dst_card)
	{
	  error_print ("Source and destination device must be the same\n");
	  return -EINVAL;
	}
      dst_path = cli_get_path (device_dst_path);
    }
  else
    {
      f = fs_ops->rename;
      dst_path = device_dst_path;
    }

  CHECK_FS_OPS_FUNC (f);
  err = f (&backend, src_path, dst_path);
  return err;
}

static int
cli_info (int argc, gchar *argv[], int *optind)
{
  const gchar *device_path;
  gint err;
  gboolean first;
  GSList *e;

  if (*optind == argc)
    {
      error_print ("Device missing\n");
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

  printf ("Type: %s\n", backend.type == BE_TYPE_SYSTEM ? "SYSTEM" : "MIDI");
  printf ("Device name: %s\n", backend.name);
  printf ("Device version: %s\n", backend.version);
  printf ("Device description: %s\n", backend.description);
  printf ("Connector name: %s\n", backend.conn_name);
  printf ("Filesystems: ");

  e = backend.fs_ops;
  first = TRUE;
  while (e)
    {
      const struct fs_operations *fs_ops = e->data;
      const gchar *name = fs_ops->name;
      gboolean cli_only = fs_ops->gui_name == NULL;
      printf ("%s%s%s", first ? "" : ", ", name,
	      cli_only ? " (CLI only)" : "");
      first = FALSE;
      e = e->next;
    }
  printf ("\n");

  return EXIT_SUCCESS;
}

static int
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
      error_print ("Device missing\n");
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
  for (guint8 i = 0; i < G_MAXUINT8; i++)
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

static int
cli_upgrade_os (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *src_path;
  const gchar *device_path;
  struct idata idata;

  if (*optind == argc)
    {
      error_print ("Local path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path missing\n");
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

  err = load_file (src_path, &idata, NULL);
  if (err)
    {
      error_print ("Error while loading '%s'.\n", src_path);
    }
  else
    {
      sysex_transfer.raw = idata.content;
      sysex_transfer.active = TRUE;
      sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;
      CHECK_FS_OPS_FUNC (backend.upgrade_os);
      err = backend.upgrade_os (&backend, &sysex_transfer);
      idata_free (&idata);
    }

  return err;
}

static int
cli_download (int argc, gchar *argv[], int *optind)
{
  const gchar *src_path;
  gchar *device_src_path, *download_path;
  gint err;
  struct idata idata;

  if (*optind == argc)
    {
      error_print ("Remote path missing\n");
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

  control.active = TRUE;
  CHECK_FS_OPS_FUNC (fs_ops->download);
  err = fs_ops->download (&backend, src_path, &idata, &control);
  if (err)
    {
      goto end;
    }

  download_path = fs_ops->get_download_path (&backend, fs_ops, ".", src_path,
					     &idata);
  if (!download_path)
    {
      err = -EINVAL;
      goto cleanup;
    }

  err = fs_ops->save (download_path, &idata, &control);
  g_free (download_path);
  g_free (control.data);

cleanup:
  idata_free (&idata);
end:
  return err;
}

static int
cli_upload (int argc, gchar *argv[], int *optind)
{
  const gchar *dst_path;
  gchar *src_path, *device_dst_path, *upload_path;
  gint err;
  struct idata idata;

  if (*optind == argc)
    {
      error_print ("Local path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote path missing\n");
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

  upload_path = fs_ops->get_upload_path (&backend, fs_ops, dst_path,
					 src_path);

  control.active = TRUE;
  err = fs_ops->load (src_path, &idata, &control);
  if (err)
    {
      goto cleanup;
    }

  CHECK_FS_OPS_FUNC (fs_ops->upload);
  err = fs_ops->upload (&backend, upload_path, &idata, &control);
  g_free (control.data);
  idata_free (&idata);

cleanup:
  g_free (upload_path);
  return err;
}

static int
cli_send (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *device_dst_path, *src_file;
  struct idata idata;

  if (*optind == argc)
    {
      error_print ("Source file missing\n");
      return -EINVAL;
    }
  else
    {
      src_file = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Remote device missing\n");
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

  err = load_file (src_file, &idata, NULL);
  if (!err)
    {
      sysex_transfer.active = TRUE;
      sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;
      sysex_transfer.raw = idata.content;
      err = backend_tx_sysex (&backend, &sysex_transfer);
      idata_free (&idata);
    }

  return err;
}

static int
cli_receive (int argc, gchar *argv[], int *optind)
{
  gint err;
  const gchar *device_src_path, *dst_file;

  if (*optind == argc)
    {
      error_print ("Remote device missing\n");
      return -EINVAL;
    }
  else
    {
      device_src_path = argv[*optind];
      (*optind)++;
    }

  if (*optind == argc)
    {
      error_print ("Destination file missing\n");
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

  sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;
  sysex_transfer.batch = TRUE;

  backend_rx_drain (&backend);
  //This doesn't need to be synchronized because the CLI is not multithreaded.
  err = backend_rx_sysex (&backend, &sysex_transfer);
  if (!err)
    {
      struct idata idata;
      idata.content = sysex_transfer.raw;
      err = save_file (dst_file, &idata, NULL);
    }

  free_msg (sysex_transfer.raw);
  return err;
}

static gint
set_conn_fs_op_from_command (const gchar *cmd)
{
  gchar *aux;

  connector = strdup (cmd);
  aux = strchr (connector, '-');
  if (!aux)
    {
      g_free (connector);
      return -EINVAL;
    }
  *aux = 0;
  aux++;

  fs = strdup (aux);
  aux = strchr (fs, '-');
  if (!aux)
    {
      g_free (connector);
      g_free (fs);
      return -EINVAL;
    }

  *aux = 0;
  aux++;

  op = strdup (aux);

  return 0;
}

#if defined(__linux__)
static void
cli_end (int sig)
{
  g_mutex_lock (&control.mutex);
  control.active = FALSE;
  g_mutex_unlock (&control.mutex);

  g_mutex_lock (&sysex_transfer.mutex);
  sysex_transfer.active = FALSE;
  g_mutex_unlock (&sysex_transfer.mutex);
}
#endif

int
main (int argc, gchar *argv[])
{
  gint c;
  gint err;
  gchar *command;
  gint vflg = 0, errflg = 0;
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
      err = set_conn_fs_op_from_command (command);
      if (err)
	{
	  goto end;
	}

      debug_print (1,
		   "Connector: \"%s\"; filesystem: \"%s\"; operation: \"%s\"\n",
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
	  err = cli_download (argc, argv, &optind);
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
	  error_print ("Command '%s' not recognized\n", command);
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
      error_print ("Error: %s\n", g_strerror (-err));
    }

  usleep (BE_REST_TIME_US * 2);
  return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
