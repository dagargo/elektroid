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

#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include "connector.h"
#include "sample.h"
#include "utils.h"

static struct connector connector;
static struct job_control control;
static const struct fs_operations *fs_ops_samples;
static const struct fs_operations *fs_ops_raw;
static const struct fs_operations *fs_ops_data;

typedef void (*print_item) (struct item_iterator *);

static void
print_sample (struct item_iterator *iter)
{
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  struct connector_iterator_data *data = iter->data;

  printf ("%c %10s %08x %s\n", iter->item.type,
	  hsize, data->cksum, iter->item.name);
  g_free (hsize);
}

static void
print_datum (struct item_iterator *iter)
{
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  struct connector_iterator_data *data = iter->data;

  printf ("%c %3d %04x %d %d %10s %s\n", iter->item.type,
	  iter->item.index, data->operations, data->has_valid_data,
	  data->has_metadata, hsize, iter->item.name);
  g_free (hsize);
}

static void
null_control_callback (gdouble foo)
{
}

static const gchar *
cli_get_path (gchar * device_path)
{
  gint len = strlen (device_path);
  char *path = device_path;
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
  struct connector_system_device device;
  GArray *devices = connector_get_system_devices ();

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct connector_system_device, i);
      printf ("%d %s\n", device.card, device.name);
    }

  g_array_free (devices, TRUE);

  return EXIT_SUCCESS;
}

static gint
cli_connect (const char *device_path)
{
  gint card = atoi (device_path);
  return connector_init (&connector, card);
}

static int
cli_list (int argc, char *argv[], int optind, fs_init_iter_func readdir,
	  print_item print)
{
  const gchar *path;
  struct item_iterator iter;
  gchar *device_path;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path))
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  if (readdir (&iter, path, &connector))
    {
      return EXIT_FAILURE;
    }

  while (!next_item_iterator (&iter))
    {
      print (&iter);
    }

  free_item_iterator (&iter);

  return EXIT_SUCCESS;
}

static int
cli_command_path (int argc, char *argv[], int optind, fs_path_func f)
{
  const gchar *path;
  gchar *device_path;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path))
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  return f (path, &connector) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_command_src_dst (int argc, char *argv[], int optind, fs_src_dst_func f)
{
  const gchar *src_path, *dst_path;
  gchar *device_src_path, *device_dst_path;
  gint src_card;
  gint dst_card;

  if (optind == argc)
    {
      error_print ("Remote path source missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_src_path = argv[optind];
      optind++;
    }

  if (optind == argc)
    {
      error_print ("Remote path destination missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_dst_path = argv[optind];
    }

  src_card = atoi (device_src_path);
  dst_card = atoi (device_dst_path);
  if (src_card != dst_card)
    {
      error_print ("Source and destination device must be the same\n");
      return EXIT_FAILURE;
    }

  if (cli_connect (device_src_path))
    {
      return EXIT_FAILURE;
    }

  src_path = cli_get_path (device_src_path);
  dst_path = cli_get_path (device_dst_path);

  return f (src_path, dst_path, &connector) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_info (int argc, char *argv[], int optind)
{
  gchar *device_path;

  if (optind == argc)
    {
      error_print ("Device missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path))
    {
      return EXIT_FAILURE;
    }

  printf ("%s\n", connector.device_name);

  return EXIT_SUCCESS;
}

static int
cli_df (int argc, char *argv[], int optind)
{
  gchar *device_path;
  gchar *size;
  gchar *diff;
  gchar *free;
  gint res;
  struct connector_storage_stats statfs;
  enum connector_storage storage;

  if (optind == argc)
    {
      error_print ("Device missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path))
    {
      return EXIT_FAILURE;
    }

  if (!connector.device_desc->storages)
    {
      return EXIT_FAILURE;
    }

  printf ("%-10.10s%16.16s%16.16s%16.16s%11.10s\n", "Storage", "Size",
	  "Used", "Available", "Use%");

  res = 0;
  for (storage = STORAGE_PLUS_DRIVE; storage <= STORAGE_RAM; storage <<= 1)
    {
      if (connector.device_desc->storages & storage)
	{
	  res |= connector_get_storage_stats (&connector, storage, &statfs);
	  size = get_human_size (statfs.bsize, FALSE);
	  diff = get_human_size (statfs.bsize - statfs.bfree, FALSE);
	  free = get_human_size (statfs.bfree, FALSE);
	  printf ("%-10.10s%16s%16s%16s%10.2f%%\n",
		  statfs.name, size, diff, free,
		  connector_get_storage_stats_percent (&statfs));
	  g_free (size);
	  g_free (diff);
	  g_free (free);
	}
    }

  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_download (int argc, char *argv[], int optind,
	      const struct fs_operations *fs_ops)
{
  const gchar *src_path;
  gchar *device_src_path, *download_path;
  gint res;
  GByteArray *array;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_src_path = argv[optind];
    }

  if (cli_connect (device_src_path))
    {
      return EXIT_FAILURE;
    }

  src_path = cli_get_path (device_src_path);

  control.active = TRUE;
  control.callback = null_control_callback;
  array = g_byte_array_new ();
  res = fs_ops->download (src_path, array, &control, &connector);
  if (res)
    {
      goto end;
    }

  download_path = connector_get_download_path (&connector, NULL, fs_ops, ".",
					       src_path);
  if (!download_path)
    {
      res = -1;
      goto end;
    }

  res = fs_ops->save (download_path, array, NULL);
  free (download_path);

end:
  g_byte_array_free (array, TRUE);

  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_upload (int argc, char *argv[], int optind,
	    const struct fs_operations *fs_ops)
{
  const gchar *dst_dir;
  gchar *src_path, *device_dst_path, *upload_path;
  gint res;
  GByteArray *array;
  gint32 index = 1;

  if (optind == argc)
    {
      error_print ("Local path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      src_path = argv[optind];
      optind++;
    }

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_dst_path = argv[optind];
    }

  if (cli_connect (device_dst_path))
    {
      return EXIT_FAILURE;
    }

  dst_dir = cli_get_path (device_dst_path);

  upload_path =
    connector_get_upload_path (&connector, NULL, fs_ops, dst_dir, src_path,
			       &index);

  array = g_byte_array_new ();

  res = fs_ops->load (src_path, array, NULL);
  if (res)
    {
      goto cleanup;
    }

  control.active = TRUE;
  control.callback = null_control_callback;
  res = fs_ops->upload (upload_path, array, &control, &connector);

cleanup:
  free (upload_path);
  g_byte_array_free (array, TRUE);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void
cli_end (int sig)
{
  control.active = FALSE;
}

int
main (int argc, char *argv[])
{
  gint c;
  gint res;
  gchar *command;
  gint vflg = 0, errflg = 0;
  struct sigaction action, old_action;

  action.sa_handler = cli_end;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;

  sigaction (SIGTERM, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    {
      sigaction (SIGTERM, &action, NULL);
    }

  sigaction (SIGQUIT, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    {
      sigaction (SIGQUIT, &action, NULL);
    }

  sigaction (SIGINT, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    {
      sigaction (SIGINT, &action, NULL);
    }

  sigaction (SIGHUP, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    {
      sigaction (SIGHUP, &action, NULL);
    }

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
      char *exec_name = basename (argv[0]);
      fprintf (stderr, "Usage: %s [options] command\n", exec_name);
      exit (EXIT_FAILURE);
    }

  fs_ops_samples = connector_get_fs_operations (FS_SAMPLES);
  fs_ops_raw = connector_get_fs_operations (FS_RAW_ALL);
  fs_ops_data = connector_get_fs_operations (FS_DATA_ALL);

  if (strcmp (command, "ld") == 0 || strcmp (command, "list-devices") == 0)
    {
      res = cli_ld ();
    }
  else if (strcmp (command, "ls") == 0
	   || strcmp (command, "list-samples") == 0)
    {
      res =
	cli_list (argc, argv, optind, fs_ops_samples->readdir, print_sample);
    }
  else if (strcmp (command, "mkdir") == 0
	   || strcmp (command, "mkdir-samples") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops_samples->mkdir);
    }
  else if (strcmp (command, "mv") == 0 || strcmp (command, "mv-sample") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops_samples->move);
    }
  else if (strcmp (command, "rm") == 0 || strcmp (command, "rm-sample") == 0
	   || strcmp (command, "rmdir") == 0
	   || strcmp (command, "rmdir-samples") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops_samples->delete);
    }
  else if (strcmp (command, "download") == 0
	   || strcmp (command, "download-sample") == 0)
    {
      res = cli_download (argc, argv, optind, fs_ops_samples);
    }
  else if (strcmp (command, "upload") == 0
	   || strcmp (command, "upload-sample") == 0)
    {
      res = cli_upload (argc, argv, optind, fs_ops_samples);
    }
  else if (strcmp (command, "info") == 0
	   || strcmp (command, "info-device") == 0)
    {
      res = cli_info (argc, argv, optind);
    }
  else if (strcmp (command, "df") == 0
	   || strcmp (command, "info-storage") == 0)
    {
      res = cli_df (argc, argv, optind);
    }
  else if (strcmp (command, "list-data") == 0)
    {
      res = cli_list (argc, argv, optind, fs_ops_data->readdir, print_datum);
    }
  else if (strcmp (command, "clear-data") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops_data->clear);
    }
  else if (strcmp (command, "copy-data") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops_data->copy);
    }
  else if (strcmp (command, "swap-data") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops_data->swap);
    }
  else if (strcmp (command, "move-data") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops_data->move);
    }
  else if (strcmp (command, "download-data") == 0)
    {
      res = cli_download (argc, argv, optind, fs_ops_data);
    }
  else if (strcmp (command, "upload-data") == 0)
    {
      res = cli_upload (argc, argv, optind, fs_ops_data);
    }
  else if (strcmp (command, "list-raw") == 0)
    {
      res = cli_list (argc, argv, optind, fs_ops_raw->readdir, print_sample);
    }
  else if (strcmp (command, "mkdir-raw") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops_raw->mkdir);
    }
  else if (strcmp (command, "rm-raw") == 0
	   || strcmp (command, "rmdir-raw") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops_raw->delete);
    }
  else if (strcmp (command, "mv-raw") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops_raw->move);
    }
  else
    {
      error_print ("Command %s not recognized\n", command);
      res = EXIT_FAILURE;
    }

  if (connector_check (&connector))
    {
      connector_destroy (&connector);
    }

  usleep (REST_TIME);

  return res;
}
