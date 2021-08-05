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

#define KIB_FLOAT 1024.0
#define MIB_FLOAT (KIB_FLOAT * KIB_FLOAT)

static struct connector connector;
static struct transfer_control control;
static const struct fs_operations *fs_ops_samples;
static const struct fs_operations *fs_ops_data;

typedef void (*print_item) (struct item_iterator *);

static void
print_sample (struct item_iterator *iter)
{
  struct connector_iterator_data *data = iter->data;

  printf ("%c %.2f %08x %s\n", iter->item.type,
	  iter->item.size / MIB_FLOAT, data->cksum, iter->item.name);
}

static void
print_datum (struct item_iterator *iter)
{
  struct connector_iterator_data *data = iter->data;

  printf ("%c %3d %04x %d %d %.2f %s\n", iter->item.type, iter->item.index,
	  data->operations, data->has_valid_data,
	  data->has_metadata, iter->item.size / MIB_FLOAT, iter->item.name);
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
cli_list (int argc, char *argv[], int optind, fs_read_dir_func readdir,
	  print_item print)
{
  const gchar *path;
  struct item_iterator *iter;
  gchar *device_path;
  gint res;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  res = cli_connect (device_path);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  iter = readdir (path, &connector);
  if (!iter)
    {
      return EXIT_FAILURE;
    }

  while (!next_item_iterator (iter))
    {
      print (iter);
    }

  free_item_iterator (iter);

  return EXIT_SUCCESS;
}

static int
cli_command_path (int argc, char *argv[], int optind, fs_path_func f)
{
  const gchar *path;
  gchar *device_path;
  gint res;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  res = cli_connect (device_path);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  return f (path, &connector);
}

static int
cli_command_src_dst (int argc, char *argv[], int optind, fs_src_dst_func f)
{
  const gchar *path_src, *path_dst;
  gchar *device_path_src, *device_path_dst;
  gint card_src;
  gint card_dst;
  gint res;

  if (optind == argc)
    {
      error_print ("Remote path source missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path_src = argv[optind];
      optind++;
    }

  if (optind == argc)
    {
      error_print ("Remote path destination missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path_dst = argv[optind];
    }

  card_src = atoi (device_path_src);
  card_dst = atoi (device_path_dst);
  if (card_src != card_dst)
    {
      error_print ("Source and destination device must be the same\n");
      return EXIT_FAILURE;
    }

  res = cli_connect (device_path_src);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  path_src = cli_get_path (device_path_src);
  path_dst = cli_get_path (device_path_dst);

  return f (path_src, path_dst, &connector);
}

static int
cli_download_sample (int argc, char *argv[], int optind)
{
  const gchar *path_src;
  gchar *device_path, *local_path;
  gint res;
  GByteArray *data;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  res = cli_connect (device_path);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  path_src = cli_get_path (device_path);

  control.active = TRUE;
  control.progress = NULL;
  data = fs_ops_samples->download (path_src, &control, &connector);

  if (data == NULL)
    {
      return EXIT_FAILURE;
    }

  local_path =
    connector_get_local_dst_path (&connector, fs_ops_samples, path_src, ".");

  res = sample_save (data, local_path);

  free (local_path);
  g_byte_array_free (data, TRUE);

  return res;
}

static int
cli_upload_sample (int argc, char *argv[], int optind)
{
  const gchar *device_dir_dst;
  gchar *path_src, *device_path_dst, *path_dst;
  gint res;
  ssize_t bytes;
  gchar *namec, *name;
  GByteArray *sample;

  if (optind == argc)
    {
      error_print ("Local path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      path_src = argv[optind];
      optind++;
    }

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path_dst = argv[optind];
    }

  res = cli_connect (device_path_dst);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  namec = strdup (path_src);
  name = basename (namec);
  remove_ext (name);
  device_dir_dst = cli_get_path (device_path_dst);
  path_dst = chain_path (device_dir_dst, name);

  sample = g_byte_array_new ();
  if (sample_load (sample, NULL, NULL, path_src, NULL, NULL))
    {
      res = EXIT_FAILURE;
      goto cleanup;
    }

  control.active = TRUE;
  control.progress = NULL;
  bytes = fs_ops_samples->upload (sample, path_dst, &control, &connector);

  res = bytes < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

cleanup:
  free (namec);
  free (path_dst);
  g_byte_array_free (sample, TRUE);
  return res;
}

static int
cli_info (int argc, char *argv[], int optind)
{
  gchar *device_path;
  gint res;

  if (optind == argc)
    {
      error_print ("Device missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  res = cli_connect (device_path);

  if (res < 0)
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

  res = cli_connect (device_path);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  printf ("%-10.10s%16.16s%16.16s%16.16s%11.10s\n", "Filesystem", "Size",
	  "Used", "Available", "Use%");

  for (storage = STORAGE_PLUS_DRIVE; storage <= STORAGE_RAM; storage <<= 1)
    {
      if (connector.device_desc->storages & storage)
	{
	  res = connector_get_storage_stats (&connector, storage, &statfs);
	  printf ("%-10.10s%16" PRId64 "%16" PRId64 "%16" PRId64 "%10.2f%%\n",
		  statfs.name, statfs.bsize, statfs.bsize - statfs.bfree,
		  statfs.bfree,
		  connector_get_storage_stats_percent (&statfs));
	}
    }

  return EXIT_SUCCESS;
}

static int
cli_download_data (int argc, char *argv[], int optind)
{
  const gchar *path;
  gchar *device_path, *local_path;
  FILE *file;
  gint res;
  GByteArray *data;
  ssize_t bytes;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  res = cli_connect (device_path);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  control.active = TRUE;
  control.progress = NULL;
  data = fs_ops_data->download (path, &control, &connector);

  if (data == NULL)
    {
      return EXIT_FAILURE;
    }

  local_path =
    connector_get_local_dst_path (&connector, fs_ops_data, path, ".");
  if (!local_path)
    {
      return EXIT_FAILURE;
    }

  file = fopen (local_path, "w");
  bytes = fwrite (data->data, 1, data->len, file);
  fclose (file);

  free (local_path);
  g_byte_array_free (data, TRUE);

  return bytes > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
cli_upload_data (int argc, char *argv[], int optind)
{
  const gchar *device_dir_dst;
  gchar *path_src, *device_path_dst, *path_dst;
  gint res;
  ssize_t bytes;
  GByteArray *datum;

  if (optind == argc)
    {
      error_print ("Local path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      path_src = argv[optind];
      optind++;
    }

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path_dst = argv[optind];
    }

  res = cli_connect (device_path_dst);

  if (res < 0)
    {
      return EXIT_FAILURE;
    }

  device_dir_dst = cli_get_path (device_path_dst);
  path_dst =
    connector_get_remote_name (&connector, fs_ops_data, device_dir_dst, NULL);

  datum = g_byte_array_new ();

  if (load_file (datum, path_src) < 0)
    {
      res = EXIT_FAILURE;
      goto cleanup;
    }

  control.active = TRUE;
  control.progress = NULL;
  bytes = fs_ops_data->upload (datum, path_dst, &control, &connector);

  res = bytes < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

cleanup:
  free (path_dst);
  g_byte_array_free (datum, TRUE);
  return res;
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
  fs_ops_data = connector_get_fs_operations (FS_DATA);

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
      res = cli_download_sample (argc, argv, optind);
    }
  else if (strcmp (command, "upload") == 0
	   || strcmp (command, "upload-sample") == 0)
    {
      res = cli_upload_sample (argc, argv, optind);
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
      res = cli_download_data (argc, argv, optind);
    }
  else if (strcmp (command, "upload-data") == 0)
    {
      res = cli_upload_data (argc, argv, optind);
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
  return res;
}
