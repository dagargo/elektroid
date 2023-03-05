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
#include <inttypes.h>
#include "../config.h"

#define CONF_DIR "~/.config/" PACKAGE

#define LABEL_MAX 256

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

//name must be filled up always. If no name is available, this can be a string representation of the ID without padding. See set_item_name_from_id function.
//In slot mode, id needs to be filled up and will typically be the MIDI preset number (the id is the filename).
//In default mode (not slot mode), id can be used for any or no purpose. It's still possible to use the id as the filename by using the FS_OPTION_ID_AS_FILENAME option.
//A value of -1 in size will show nothing on the interface. If the size column is not used at all, use FS_OPTION_SHOW_SIZE_COLUMN.

struct item
{
  enum item_type type;
  gchar name[LABEL_MAX];
  gint32 id;
  gint64 size;
};

struct item_iterator
{
  iterator_next next;
  iterator_free free;
  void *data;
  struct item item;
};

struct fs_operations;

typedef void (*fs_print_item) (struct item_iterator *, struct backend *,
			       const struct fs_operations *);

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

typedef gchar *(*fs_get_item_slot) (struct item *, struct backend *);

typedef gint (*fs_local_file_op) (const gchar *, GByteArray *,
				  struct job_control *);

typedef gchar *(*fs_get_ext) (struct backend *, const struct fs_operations *);

typedef gchar *(*fs_get_upload_path) (struct backend *,
				      const struct fs_operations *,
				      const gchar *, const gchar *);

typedef gchar *(*fs_get_download_path) (struct backend *,
					const struct fs_operations *,
					const gchar *, const gchar *,
					GByteArray *);

typedef void (*fs_select_item) (struct backend *, const gchar *,
				struct item *);

typedef gint (*t_sysex_transfer) (struct backend *, struct sysex_transfer *);

// All the function members that return gint should return 0 if no error and a negative number in case of error.
// errno values are recommended as will provide the user with a meaningful message. In particular,
// ENOSYS could be used when a particular device does not support a feature that other devices implementing the same filesystem do.

// rename and move are different operations. If move is implemented, rename must behave the same way. However, t's perfectly
// possible to implement rename without implementing move. This is the case in slot mode filesystems.

struct fs_operations
{
  gint32 fs;
  guint32 options;
  const gchar *name;
  const gchar *gui_name;
  const gchar *gui_icon;
  const gchar *type_ext;
  guint32 max_name_len;
  fs_init_iter_func readdir;	//This function runs on its own thread so it can take as long as needed in order to make calls to next_item_iterator not to wait for IO.
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
  fs_get_item_slot get_slot;
  fs_local_file_op save;
  fs_local_file_op load;
  fs_get_ext get_ext;
  fs_get_upload_path get_upload_path;
  fs_get_download_path get_download_path;
  fs_select_item select_item;
};

enum fs_options
{
  //Show the audio player.
  FS_OPTION_AUDIO_PLAYER = 0x1,
  //Allow stereo samples. Only useful if used together with FS_OPTION_AUDIO_PLAYER
  FS_OPTION_STEREO = 0x2,
  //Every operation will block the remote browser.
  FS_OPTION_SINGLE_OP = 0x4,
  //Filename is the ID instead of the name. Useful when the device allows different items to have the same name.
  FS_OPTION_ID_AS_FILENAME = 0x8,
  //In slot mode, dst_path passed to t_get_upload_path includes the ID, a colon (':') and the system filename.
  //Also, as every destination slot is always used, drop is only possible over a concrete slot.
  //A DND operation of several items over a slot will behave as dropping the first item over the destination slot and the rest over the following ones.
  //Typically used together with FS_OPTION_ID_AS_FILENAME but not necessary.
  FS_OPTION_SLOT_STORAGE = 0x10,
  //Show column options. Name column is always showed.
  FS_OPTION_SHOW_ID_COLUMN = 0x20,
  FS_OPTION_SHOW_SIZE_COLUMN = 0x40,
  FS_OPTION_SHOW_SLOT_COLUMN = 0x80,
  //Sort items options.
  FS_OPTION_SORT_BY_ID = 0x100,
  FS_OPTION_SORT_BY_NAME = 0x200
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

/**
 * Returns the filename for an item, which is a string that uniquely idenfifies an item.
 * In a PC, filenames are typically strings but in embedded devices this could be just a number (in string format).
 * Typically, in these systems, several slots can have the same name but the id is an address to a memory slot.
 * @param options The options member in the fs_operations struct.
 * @param item
 */
gchar *get_filename (guint32 options, struct item *item);

guint next_item_iterator (struct item_iterator *);

void free_item_iterator (struct item_iterator *);

gint copy_item_iterator (struct item_iterator *, struct item_iterator *,
			 gboolean);

gint load_file (const char *, GByteArray *, struct job_control *);

gint save_file (const char *, GByteArray *, struct job_control *);

gint save_file_char (const gchar *, const guint8 *, ssize_t);

gchar *get_human_size (gint64, gboolean);

void set_job_control_progress_with_cb (struct job_control *, gdouble,
				       gpointer);

void set_job_control_progress (struct job_control *, gdouble);

void set_job_control_progress_no_sync (struct job_control *, gdouble,
				       gpointer);

gboolean file_matches_extensions (const gchar *, gchar **);

gboolean iter_matches_extensions (struct item_iterator *, gchar **);

#endif
