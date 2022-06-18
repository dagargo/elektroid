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
#include <stddef.h>
#include "connector.h"
#include "utils.h"

#define GET_FS_OPS_OFFSET(member) offsetof(struct fs_operations, member)
#define GET_FS_OPS_FUNC(type,fs,offset) (*(((type *) (((gchar *) fs) + offset))))
#define CHECK_FS_OPS_FUNC(f) if (!(f)) {error_print ("Operation not implemented\n"); return EXIT_FAILURE;}

static struct connector connector;
static struct job_control control;
static const gchar *devices_filename;

typedef void (*print_item) (struct item_iterator *);

static void
print_smplrw (struct item_iterator *iter)
{
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  struct connector_iterator_data *data = iter->data;

  printf ("%c %10s %08x %s\n", iter->item.type,
	  hsize, data->hash, iter->item.name);
  g_free (hsize);
}

static void
print_data (struct item_iterator *iter)
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
cli_connect (const gchar * device_path, const struct fs_operations *fs_ops)
{
  gint card = atoi (device_path);
  gint ret = connector_init (&connector, card, devices_filename);
  if (!ret && fs_ops && !(connector.device_desc.filesystems & fs_ops->fs))
    {
      error_print ("Filesystem not supported for device '%s'\n",
		   connector.device_desc.name);
      return 1;
    }
  return ret;
}

static int
cli_list (int argc, gchar * argv[], int optind,
	  const struct fs_operations *fs_ops, print_item print)
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

  if (cli_connect (device_path, fs_ops))
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  CHECK_FS_OPS_FUNC (fs_ops->readdir);
  if (fs_ops->readdir (&iter, path, &connector))
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
cli_command_path (int argc, gchar * argv[], int optind,
		  const struct fs_operations *fs_ops, ssize_t member_offset)
{
  const gchar *path;
  gchar *device_path;
  gint ret;
  fs_path_func f;

  if (optind == argc)
    {
      error_print ("Remote path missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path, fs_ops))
    {
      return EXIT_FAILURE;
    }

  path = cli_get_path (device_path);

  f = GET_FS_OPS_FUNC (fs_path_func, fs_ops, member_offset);
  CHECK_FS_OPS_FUNC (f);
  ret = f (path, &connector);
  return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_command_src_dst (int argc, gchar * argv[], int optind,
		     const struct fs_operations *fs_ops,
		     ssize_t member_offset)
{
  const gchar *src_path, *dst_path;
  gchar *device_src_path, *device_dst_path;
  gint src_card;
  gint dst_card;
  int ret;
  fs_src_dst_func f;

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

  if (cli_connect (device_src_path, 0))
    {
      return EXIT_FAILURE;
    }

  f = GET_FS_OPS_FUNC (fs_src_dst_func, fs_ops, member_offset);
  CHECK_FS_OPS_FUNC (f);
  src_path = cli_get_path (device_src_path);
  dst_path = cli_get_path (device_dst_path);
  ret = f (src_path, dst_path, &connector);
  return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_info (int argc, gchar * argv[], int optind)
{
  gchar *device_path;
  gchar *comma;

  if (optind == argc)
    {
      error_print ("Device missing\n");
      return EXIT_FAILURE;
    }
  else
    {
      device_path = argv[optind];
    }

  if (cli_connect (device_path, 0))
    {
      return EXIT_FAILURE;
    }

  printf ("%s filesystems=", connector.device_name);
  comma = "";
  int fs = 1;
  while (fs <= sizeof (int) * 8)
    {
      if (connector.device_desc.filesystems & fs)
	{
	  printf ("%s%s", comma,
		  connector_get_fs_operations (fs, NULL)->name);
	  comma = ",";
	}
      fs <<= 1;
    }
  printf ("\n");

  return EXIT_SUCCESS;
}

static int
cli_df (int argc, gchar * argv[], int optind)
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

  if (cli_connect (device_path, 0))
    {
      return EXIT_FAILURE;
    }

  if (!connector.device_desc.storage)
    {
      return EXIT_FAILURE;
    }

  printf ("%-10.10s%16.16s%16.16s%16.16s%11.10s\n", "Storage", "Size",
	  "Used", "Available", "Use%");

  res = 0;
  for (storage = STORAGE_PLUS_DRIVE; storage <= STORAGE_RAM; storage <<= 1)
    {
      if (connector.device_desc.storage & storage)
	{
	  res |= connector_get_storage_stats (&connector, storage, &statfs);
	  if (res)
	    {
	      continue;
	    }
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
cli_upgrade_os (int argc, gchar * argv[], int optind)
{
  gint res;
  const gchar *src_path;
  const gchar *device_path;
  struct sysex_transfer sysex_transfer;

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
      device_path = argv[optind];
    }

  if (cli_connect (device_path, 0))
    {
      return EXIT_FAILURE;
    }

  sysex_transfer.raw = g_byte_array_new ();
  res = load_file (src_path, sysex_transfer.raw, NULL);
  if (res)
    {
      error_print ("Error while loading '%s'.\n", src_path);
    }
  else
    {
      sysex_transfer.active = TRUE;
      sysex_transfer.timeout = SYSEX_TIMEOUT_MS;
      CHECK_FS_OPS_FUNC (connector.upgrade_os);
      res = connector.upgrade_os (&sysex_transfer, &connector);
    }

  g_byte_array_free (sysex_transfer.raw, TRUE);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_download (int argc, gchar * argv[], int optind,
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

  if (cli_connect (device_src_path, fs_ops))
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

  download_path = fs_ops->get_download_path (fs_ops, ".", src_path, NULL,
					     &connector);
  if (!download_path)
    {
      res = -1;
      goto end;
    }

  res = fs_ops->save (download_path, array, &control);
  g_free (download_path);
  g_free (control.data);

end:
  g_byte_array_free (array, TRUE);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int
cli_upload (int argc, gchar * argv[], int optind,
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

  if (cli_connect (device_dst_path, fs_ops))
    {
      return EXIT_FAILURE;
    }

  dst_dir = cli_get_path (device_dst_path);

  upload_path = fs_ops->get_upload_path (fs_ops, dst_dir, src_path, &index,
					 NULL, &connector);

  array = g_byte_array_new ();
  control.active = TRUE;
  control.callback = null_control_callback;
  res = fs_ops->load (src_path, array, &control);
  if (res)
    {
      goto cleanup;
    }

  res = fs_ops->upload (upload_path, array, &control, &connector);
  g_free (control.data);

cleanup:
  g_free (upload_path);
  g_byte_array_free (array, TRUE);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

static const struct fs_operations *
get_op_type_from_command (const gchar * cmd, gchar * op, gchar * type)
{
  gint i;
  gchar *c;
  gint len;

  strncpy (op, cmd, LABEL_MAX);
  op[LABEL_MAX - 1] = 0;
  len = strlen (op);
  c = op;
  for (i = 0; i < LABEL_MAX && i < strlen (op); i++, c++)
    {
      if (*c == '-')
	{
	  *c = 0;
	  break;
	}
    }
  if (i < len - 1)
    {
      c++;
      strncpy (type, c, LABEL_MAX);
      type[LABEL_MAX - 1] = 0;
    }
  else
    {
      *type = 0;
    }

  return connector_get_fs_operations (-1, type);
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
  gint res;
  gchar *command;
  gint vflg = 0, errflg = 0;
  struct sigaction action;
  const struct fs_operations *fs_ops;
  gchar op[LABEL_MAX];
  gchar type[LABEL_MAX];

  action.sa_handler = cli_end;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGHUP, &action, NULL);

  devices_filename = NULL;

  while ((c = getopt (argc, argv, "f:v")) != -1)
    {
      switch (c)
	{
	case 'f':
	  devices_filename = optarg;
	  break;
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

  fs_ops = get_op_type_from_command (command, op, type);
  debug_print (1, "Operation: '%s'; filesystem: '%s'\n", op, type);

  if (!fs_ops)
    {
      if (strcmp (command, "ld") == 0
	  || strcmp (command, "list-devices") == 0)
	{
	  res = cli_ld ();
	}
      else if (strcmp (op, "info") == 0
	       || strcmp (command, "info-devices") == 0)
	{
	  res = cli_info (argc, argv, optind);
	}
      else if (strcmp (command, "df") == 0
	       || strcmp (command, "info-storage") == 0)
	{
	  res = cli_df (argc, argv, optind);
	}
      else if (strcmp (command, "upgrade") == 0)
	{
	  res = cli_upgrade_os (argc, argv, optind);
	}
      else if (strcmp (command, "ls") == 0)
	{
	  res =
	    cli_list (argc, argv, optind,
		      connector_get_fs_operations (FS_SAMPLES, NULL),
		      print_smplrw);
	}
      else if (strcmp (command, "mkdir") == 0)
	{
	  res =
	    cli_command_path (argc, argv, optind,
			      connector_get_fs_operations (FS_SAMPLES, NULL),
			      GET_FS_OPS_OFFSET (mkdir));
	}
      else if (strcmp (command, "mv") == 0)
	{
	  res =
	    cli_command_src_dst (argc, argv, optind,
				 connector_get_fs_operations (FS_SAMPLES,
							      NULL),
				 GET_FS_OPS_OFFSET (move));
	}
      else if (strcmp (command, "rm") == 0 || strcmp (command, "rmdir") == 0)
	{
	  res =
	    cli_command_path (argc, argv, optind,
			      connector_get_fs_operations (FS_SAMPLES, NULL),
			      GET_FS_OPS_OFFSET (delete));
	}
      else if (strcmp (command, "download") == 0
	       || strcmp (command, "dl") == 0)
	{
	  res =
	    cli_download (argc, argv, optind,
			  connector_get_fs_operations (FS_SAMPLES, NULL));
	}
      else if (strcmp (command, "upload") == 0 || strcmp (command, "ul") == 0)
	{
	  res =
	    cli_upload (argc, argv, optind,
			connector_get_fs_operations (FS_SAMPLES, NULL));
	}
      else
	{
	  error_print ("Command '%s' not recognized\n", command);
	  res = EXIT_FAILURE;
	}
      goto end;
    }

  if (strcmp (op, "ls") == 0 || strcmp (op, "list") == 0)
    {
      print_item print = (fs_ops->fs == FS_SAMPLES || fs_ops->fs == FS_RAW_ALL
			  || fs_ops->fs ==
			  FS_RAW_PRESETS) ? print_smplrw : print_data;
      res = cli_list (argc, argv, optind, fs_ops, print);
    }
  else if (strcmp (op, "mkdir") == 0)
    {
      res =
	cli_command_path (argc, argv, optind, fs_ops,
			  GET_FS_OPS_OFFSET (mkdir));
    }
  else if (strcmp (op, "rm") == 0 || strcmp (op, "rmdir") == 0)
    {
      res =
	cli_command_path (argc, argv, optind, fs_ops,
			  GET_FS_OPS_OFFSET (delete));
    }
  else if (strcmp (op, "download") == 0 || strcmp (op, "dl") == 0)
    {
      res = cli_download (argc, argv, optind, fs_ops);
    }
  else if (strcmp (op, "upload") == 0 || strcmp (op, "ul") == 0)
    {
      res = cli_upload (argc, argv, optind, fs_ops);
    }
  else if (strcmp (op, "cl") == 0)
    {
      res = cli_command_path (argc, argv, optind, fs_ops,
			      GET_FS_OPS_OFFSET (clear));
    }
  else if (strcmp (op, "cp") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops,
				 GET_FS_OPS_OFFSET (copy));
    }
  else if (strcmp (op, "sw") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops,
				 GET_FS_OPS_OFFSET (swap));
    }
  else if (strcmp (op, "mv") == 0)
    {
      res = cli_command_src_dst (argc, argv, optind, fs_ops,
				 GET_FS_OPS_OFFSET (move));
    }
  else
    {
      error_print ("Command '%s' not recognized\n", command);
      res = EXIT_FAILURE;
    }

end:
  if (connector_check (&connector))
    {
      connector_destroy (&connector);
    }

  usleep (REST_TIME_US * 2);
  return res;
}
