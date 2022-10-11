/*
 *   utils.h
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

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <unistd.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include "../config.h"

#define CONF_DIR "~/.config/" PACKAGE

#define LABEL_MAX 256

#define AUDIO_SAMPLE_RATE 48000

#define MAX_BACKEND_FSS (sizeof (gint32) * 8)
#define MAX_BACKEND_STORAGE MAX_BACKEND_FSS

#define debug_print(level, format, ...) if (level <= debug_level) fprintf(stderr, "DEBUG:" __FILE__ ":%d:(%s): " format, __LINE__, __FUNCTION__, ## __VA_ARGS__)
#define error_print(format, ...) fprintf(stderr, "%sERROR:" __FILE__ ":%d:(%s): " format "%s", isatty(fileno(stderr)) ? "\x1b[31m" : "", __LINE__, __FUNCTION__, ## __VA_ARGS__, isatty(fileno(stderr)) ? "\x1b[m" : "")

enum item_type
{
  ELEKTROID_NONE = 0,
  ELEKTROID_FILE = 'F',
  ELEKTROID_DIR = 'D'
};

struct backend;

struct item_iterator;

typedef guint (*iterator_next) (struct item_iterator *);

typedef void (*iterator_free) (void *);

typedef gint (*iterator_copy) (struct item_iterator *, struct item_iterator *,
			       gboolean);

struct item
{
  gint32 id;
  gint64 size;
  enum item_type type;
  gchar name[LABEL_MAX];
};

struct item_iterator
{
  iterator_next next;
  iterator_free free;
  //copy is only needed when the FS supports directories. This does not mean that mkdir is supported, as dirs could be just a way to show the data.
  iterator_copy copy;
  void *data;
  struct item item;
};

typedef void (*fs_print_item) (struct item_iterator *);

struct job_control;

typedef void (*job_control_callback) (struct job_control *);

struct job_control
{
  gboolean active;
  GMutex mutex;
  job_control_callback callback;
  gint parts;
  gint part;
  gdouble progress;
  void *data;
};

// This contains information taken from from the sample data.
struct sample_info
{
  guint32 loopstart;
  guint32 loopend;
  guint32 looptype;
  guint32 samplerate;
  guint32 bitdepth;
  guint32 channels;
  guint32 frames;
};

// This contains the format in which data must be load.
struct sample_params
{
  guint32 channels;
  guint32 samplerate;
};

struct device_desc
{
  guint32 id;
  gchar name[LABEL_MAX];
  gchar alias[LABEL_MAX];
  guint32 filesystems;
  guint32 storage;
};

enum sysex_transfer_status
{
  WAITING,
  SENDING,
  RECEIVING,
  FINISHED
};

struct sysex_transfer
{
  gboolean active;
  GMutex mutex;
  enum sysex_transfer_status status;
  gint timeout;			//Measured in ms. -1 is infinite.
  gint time;
  gboolean batch;
  GByteArray *raw;
  gint err;
};

struct fs_operations;

typedef gint (*fs_init_iter_func) (struct backend *, struct item_iterator *,
				   const gchar *);

typedef gint (*fs_path_func) (struct backend *, const gchar *);

typedef gint (*fs_src_dst_func) (struct backend *, const gchar *,
				 const gchar *);

typedef gint (*fs_remote_file_op) (struct backend *, const gchar *,
				   GByteArray *, struct job_control *);

typedef gchar *(*fs_get_item_id) (struct item *);

typedef gint (*fs_local_file_op) (const gchar *, GByteArray *,
				  struct job_control *);

typedef gchar *(*fs_get_ext) (const struct device_desc *,
			      const struct fs_operations *);

typedef gchar *(*t_get_upload_path) (struct backend *, struct item_iterator *,
				     const struct fs_operations *,
				     const gchar *, const gchar *, gint32 *);

typedef gchar *(*t_get_download_path) (struct backend *,
				       struct item_iterator *,
				       const struct fs_operations *,
				       const gchar *, const gchar *);

typedef gint (*t_sysex_transfer) (struct backend *, struct sysex_transfer *);

struct fs_operations
{
  gint32 fs;
  guint32 options;
  const gchar *name;
  const gchar *gui_name;
  const gchar *gui_icon;
  fs_init_iter_func readdir;
  fs_print_item print_item;
  fs_path_func mkdir;
  fs_path_func delete;
  fs_src_dst_func rename;
  fs_src_dst_func move;
  fs_src_dst_func copy;
  fs_path_func clear;
  fs_src_dst_func swap;
  fs_remote_file_op download;
  fs_remote_file_op upload;
  fs_get_item_id getid;
  fs_local_file_op save;
  fs_local_file_op load;
  fs_get_ext get_ext;
  t_get_upload_path get_upload_path;
  t_get_download_path get_download_path;
  const gchar *type_ext;
};

enum fs_options
{
  FS_OPTION_AUDIO_PLAYER = 0x1,
  FS_OPTION_STEREO = 0x2,
  FS_OPTION_SHOW_INDEX_COLUMN = 0x4,
  FS_OPTION_SINGLE_OP = 0x8,
  FS_OPTION_SLOT_STORAGE = 0x10,	//In SLOT mode, dst_dir passed to t_get_upload_path includes the index, ':' and the item name.
  FS_OPTION_SORT_BY_ID = 0x20,
  FS_OPTION_SORT_BY_NAME = 0x40
};

extern int debug_level;

gchar *debug_get_hex_data (gint, guint8 *, guint);

gchar *debug_get_hex_msg (const GByteArray *);

gchar *chain_path (const gchar *, const gchar *);

void remove_ext (gchar *);

const gchar *get_ext (const gchar *);

gchar get_type_from_inventory_icon (const gchar *);

gchar *get_expanded_dir (const gchar *);

gchar *get_local_startup_path (const gchar *);

void free_msg (gpointer);

gchar *get_item_name (struct item *);

gchar *get_item_index (struct item *);

guint next_item_iterator (struct item_iterator *);

void free_item_iterator (struct item_iterator *);

gint copy_item_iterator (struct item_iterator *, struct item_iterator *,
			 gboolean);

gint load_file (const char *, GByteArray *, struct job_control *);

gint save_file (const char *, GByteArray *, struct job_control *);

gint save_file_char (const gchar *, const guint8 *, ssize_t);

gchar *get_human_size (gint64, gboolean);

void set_job_control_progress (struct job_control *, gdouble);

void set_job_control_progress_no_sync (struct job_control *, gdouble);

#endif
