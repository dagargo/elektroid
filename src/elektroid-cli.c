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
#include <libgen.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include "backend.h"
#include "connector.h"
#include "utils.h"

#define GET_FS_OPS_OFFSET(member) offsetof(struct fs_operations, member)
#define GET_FS_OPS_FUNC(type,fs,offset) (*(((type *) (((gchar *) fs) + offset))))
#define CHECK_FS_OPS_FUNC(f) if (!(f)) {return -ENOSYS;}

static struct backend backend;
static struct job_control control;
static gchar *connector, *fs, *op;
const struct fs_operations *fs_ops;

static const gchar *
cli_get_path (gchar * device_path)
{
  gint len = strlen (device_path);
  gchar *path = device_path;
  gint i = 0;

  while (path[0] != '/' && i < len)
    {
      path++;
      i++;
    }

  return path;
}

static gint
cli_ld ()
{
  gint i;
  struct backend_system_device device;
  GArray *devices = backend_get_system_devices ();

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct backend_system_device, i);
      printf ("%d: %s %s\n", i, device.id, device.name);
    }

  g_array_free (devices, TRUE);

  return EXIT_SUCCESS;
}

static gint
cli_connect (const gchar * device_path)
{
  gint err, id = (gint) atoi (device_path);
  struct backend_system_device device;
  GArray *devices = backend_get_system_devices ();

  if (!devices->len)
    {
      error_print ("Invalid device %d\n", id);
      return -ENODEV;
    }

  device = g_array_index (devices, struct backend_system_device, id);
  err = connector_init_backend (&backend, device.id, connector, NULL);
  g_array_free (devices, TRUE);

  if (!err && fs)
    {
      fs_ops = backend_get_fs_operations (&backend, 0, fs);
      if (!fs_ops)
	{
	  error_print ("Invalid filesystem '%s'\n", fs);
	  return -EINVAL;
	}
    }

  return err;
}

static gint
cli_list (int argc, gchar * argv[], int *optind)
{
  gint err;
  const gchar *path;
  struct item_iterator iter;
  gchar *device_path;

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

  err = fs_ops->readdir (&backend, &iter, path);
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
cli_command_path (int argc, gchar * argv[], int *optind,
		  ssize_t member_offset)
{
  const gchar *path;
  gchar *device_path;
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
cli_command_src_dst (int argc, gchar * argv[], int *optind,
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
cli_command_mv_rename (int argc, gchar * argv[], int *optind)
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
cli_info (int argc, gchar * argv[], int *optind)
{
  gchar *device_path;
  const gchar *name;
  gint err;

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

  printf ("%s; version: %s; description: %s; filesystems: ",
	  backend.name, backend.version, backend.description);

  for (gint fs = 1; fs <= MAX_BACKEND_FSS; fs <<= 1)
    {
      if (backend.filesystems & fs)
	{
	  name = backend_get_fs_operations (&backend, fs, NULL)->name;
	  printf ("%s%s", fs == 1 ? "" : ",", name);
	}
    }
  printf ("\n");

  return EXIT_SUCCESS;
}

static int
cli_df (int argc, gchar * argv[], int *optind)
{
  gchar *device_path;
  gchar *size;
  gchar *diff;
  gchar *free;
  gint err, storage;
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

  if (!backend.storage || !backend.get_storage_stats)
    {
      return -ENOSYS;
    }

  printf ("%-10.10s%16.16s%16.16s%16.16s%11.10s\n", "Storage", "Size",
	  "Used", "Available", "Use%");

  err = 0;
  for (storage = 1; storage < MAX_BACKEND_STORAGE; storage <<= 1)
    {
      if (backend.storage & storage)
	{
	  err |= backend.get_storage_stats (&backend, storage, &statfs);
	  if (err)
	    {
	      continue;
	    }
	  size = get_human_size (statfs.bsize, FALSE);
	  diff = get_human_size (statfs.bsize - statfs.bfree, FALSE);
	  free = get_human_size (statfs.bfree, FALSE);
	  printf ("%-10.10s%16s%16s%16s%10.2f%%\n",
		  statfs.name, size, diff, free,
		  backend_get_storage_stats_percent (&statfs));
	  g_free (size);
	  g_free (diff);
	  g_free (free);
	}
    }

  return err;
}

static int
cli_upgrade_os (int argc, gchar * argv[], int *optind)
{
  gint err;
  const gchar *src_path;
  const gchar *device_path;
  struct sysex_transfer sysex_transfer;

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

  sysex_transfer.raw = g_byte_array_new ();
  err = load_file (src_path, sysex_transfer.raw, NULL);
  if (err)
    {
      error_print ("Error while loading '%s'.\n", src_path);
    }
  else
    {
      sysex_transfer.active = TRUE;
      sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;
      CHECK_FS_OPS_FUNC (backend.upgrade_os);
      err = backend.upgrade_os (&backend, &sysex_transfer);
    }

  g_byte_array_free (sysex_transfer.raw, TRUE);
  return err;
}

static int
cli_download (int argc, gchar * argv[], int *optind)
{
  const gchar *src_path;
  gchar *device_src_path, *download_path;
  gint err;
  GByteArray *array;

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
  array = g_byte_array_new ();
  CHECK_FS_OPS_FUNC (fs_ops->download);
  err = fs_ops->download (&backend, src_path, array, &control);
  if (err)
    {
      goto end;
    }

  download_path =
    fs_ops->get_download_path (&backend, fs_ops, ".", src_path, array);
  if (!download_path)
    {
      err = -EINVAL;
      goto end;
    }

  err = fs_ops->save (download_path, array, &control);
  g_free (download_path);
  g_free (control.data);

end:
  g_byte_array_free (array, TRUE);
  return err;
}

static int
cli_upload (int argc, gchar * argv[], int *optind)
{
  const gchar *dst_dir;
  gchar *src_path, *device_dst_path, *upload_path;
  gint err;
  GByteArray *array;
  gint32 index = 1;

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

  dst_dir = cli_get_path (device_dst_path);

  upload_path = fs_ops->get_upload_path (&backend, fs_ops, dst_dir,
					 src_path, &index);

  array = g_byte_array_new ();
  control.active = TRUE;
  err = fs_ops->load (src_path, array, &control);
  if (err)
    {
      goto cleanup;
    }

  CHECK_FS_OPS_FUNC (fs_ops->upload);
  err = fs_ops->upload (&backend, upload_path, array, &control);
  g_free (control.data);

cleanup:
  g_free (upload_path);
  g_byte_array_free (array, TRUE);
  return err;
}

static int
cli_send (int argc, gchar * argv[], int *optind)
{
  gint err;
  const gchar *device_dst_path, *src_file;
  struct sysex_transfer sysex_transfer;

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

  sysex_transfer.active = TRUE;
  sysex_transfer.timeout = BE_DUMP_TIMEOUT;
  sysex_transfer.raw = g_byte_array_new ();

  err = load_file (src_file, sysex_transfer.raw, NULL);

  if (!err)
    {
      err = backend_tx_sysex (&backend, &sysex_transfer);
    }

  free_msg (sysex_transfer.raw);

  return err;
}

static int
cli_receive (int argc, gchar * argv[], int *optind)
{
  gint err;
  const gchar *device_src_path, *dst_file;
  struct sysex_transfer sysex_transfer;

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

  sysex_transfer.timeout = BE_DUMP_TIMEOUT;
  sysex_transfer.batch = TRUE;

  backend_rx_drain (&backend);
  //This doesn't need to be synchronized because the CLI is not multithreaded.
  err = backend_rx_sysex (&backend, &sysex_transfer);
  if (!err)
    {
      err = save_file (dst_file, sysex_transfer.raw, NULL);
    }

  free_msg (sysex_transfer.raw);

  return err;
}

static gint
set_conn_fs_op_from_command (const gchar * cmd)
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

static void
cli_end (int sig)
{
  g_mutex_lock (&control.mutex);
  control.active = FALSE;
  g_mutex_unlock (&control.mutex);
}

int
main (int argc, gchar * argv[])
{
  gint c;
  gint err;
  gchar *command;
  gint vflg = 0, errflg = 0;
  struct sigaction action;

  action.sa_handler = cli_end;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGHUP, &action, NULL);

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
      gchar *exec_name = basename (argv[0]);
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
