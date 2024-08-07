/*
 *   connector.h
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

#include "backend.h"

#ifndef CONNECTOR_H
#define CONNECTOR_H

#define FS_ICON_WAVE "elektroid-wave-symbolic"
#define FS_ICON_WAVETABLE "elektroid-wavetable-symbolic"
#define FS_ICON_SEQ "elektroid-sequence-symbolic"
#define FS_ICON_PRJ "elektroid-project-symbolic"
#define FS_ICON_SND "elektroid-sound-symbolic"
#define FS_ICON_GENERIC "elektroid-file-symbolic"

enum item_type
{
  ITEM_TYPE_NONE = 0,
  ITEM_TYPE_FILE = 'F',
  ITEM_TYPE_DIR = 'D'
};

//name must be filled up always. If no name is available, this can be a string representation of the ID without padding. See set_item_name_from_id function.
//In slot mode, id needs to be filled up and will typically be the MIDI preset number (the id is the filename).
//In default mode (not slot mode), id can be used for any or no purpose. It's still possible to use the id as the filename by using the FS_OPTION_ID_AS_FILENAME option.
//A value of -1 in size will show nothing on the interface. If the size column is not used at all, do not use FS_OPTION_SHOW_SIZE_COLUMN.

struct item
{
  enum item_type type;
  gchar name[LABEL_MAX];
  gint32 id;			// Used only by slot filesystems
  gint64 size;
  //Optionally filled up structs by filesystems.
  //Filesystem options must indicate if these are in use with FS_OPTION_SHOW_SAMPLE_COLUMNS and FS_OPTION_SHOW_INFO_COLUMN.
  struct sample_info sample_info;
  gchar object_info[LABEL_MAX];
};

struct item_iterator;

typedef gint (*iterator_next) (struct item_iterator *);

typedef void (*iterator_free) (void *);

struct item_iterator
{
  gchar *dir;
  iterator_next next;
  iterator_free free;
  void *data;
  struct item item;
};

struct fs_operations;

typedef void (*fs_print_item) (struct item_iterator *, struct backend *,
			       const struct fs_operations *);

typedef gint (*fs_init_iter_func) (struct backend *, struct item_iterator *,
				   const gchar *, GSList *);

typedef gint (*fs_path_func) (struct backend *, const gchar *);

typedef gint (*fs_src_dst_func) (struct backend *, const gchar *,
				 const gchar *);

typedef gint (*fs_remote_file_op) (struct backend *, const gchar *,
				   struct idata *, struct job_control *);

typedef gchar *(*fs_get_item_slot) (struct item *, struct backend *);

typedef gint (*fs_local_file_op) (const gchar *, struct idata *,
				  struct job_control *);

typedef GSList *(*fs_get_exts) (struct backend *,
				const struct fs_operations *);

typedef gchar *(*fs_get_path) (struct backend * backend,
			       const struct fs_operations * ops,
			       const gchar * dst_dir, const gchar * src_path,
			       struct idata * idata);

typedef void (*fs_select_item) (struct backend *, const gchar *,
				struct item *);

typedef gboolean (*fs_file_exists) (struct backend *, const gchar *);

// All the function members that return gint should return 0 if no error and a negative number in case of error.
// errno values are recommended as will provide the user with a meaningful message. In particular,
// ENOSYS could be used when a particular device does not support a feature that other devices implementing the same filesystem do.

// rename and move are different operations. If move is implemented, rename must behave the same way. However, it's perfectly
// possible to implement rename without implementing move. This is the case in slot mode filesystems.

struct fs_operations
{
  gint32 id;
  guint32 options;
  const gchar *name;
  const gchar *gui_name;
  const gchar *gui_icon;
  const gchar *ext;
  guint32 max_name_len;
  fs_init_iter_func readdir;	//This function runs on its own thread so it can take as long as needed in order to make calls to item_iterator_next not to wait for IO.
  fs_file_exists file_exists;
  fs_print_item print_item;
  fs_path_func mkdir;
  fs_path_func delete;
  fs_src_dst_func rename;
  fs_src_dst_func move;
  fs_src_dst_func copy;
  fs_path_func clear;
  fs_src_dst_func swap;
  fs_remote_file_op download;	//Donload a resource from the filesystem to memory.
  fs_remote_file_op upload;	//Upload a resource from memory to the filesystem.
  fs_local_file_op save;	//Write a file from memory to the OS storage. Typically used after download.
  fs_local_file_op load;	//Load a file from the OS storage into memory. Typically used before upload.
  fs_get_item_slot get_slot;	//Optionally used by slot filesystems to show a custom slot name column such `A01` or `[P-01]`. Needs FS_OPTION_SHOW_SLOT_COLUMN.
  fs_get_exts get_exts;		//If present, this is used; if not, type_ext is used.
  fs_get_path get_upload_path;
  fs_get_path get_download_path;
  fs_select_item select_item;
};

enum fs_options
{
  //Show the audio player.
  FS_OPTION_SAMPLE_EDITOR = 0x1,
  //Allow mono samples. Only useful if used together with FS_OPTION_SAMPLE_EDITOR
  FS_OPTION_MONO = 0x2,
  //Allow stereo samples. Only useful if used together with FS_OPTION_SAMPLE_EDITOR
  FS_OPTION_STEREO = 0x4,
  //Every operation will block the remote browser.
  FS_OPTION_SINGLE_OP = 0x8,
  //In slot storage mode, the item name in dst_path passed to get_upload_path is the ID.
  //DND is only possible over a concrete slot.
  //A DND operation of several items over a slot will behave as dropping the first item over the destination slot and the rest over the following ones.
  FS_OPTION_SLOT_STORAGE = 0x10,
  //Show column options. Name column is always showed.
  FS_OPTION_SHOW_ID_COLUMN = 0x20,
  FS_OPTION_SHOW_SIZE_COLUMN = 0x40,
  FS_OPTION_SHOW_SLOT_COLUMN = 0x80,
  FS_OPTION_SHOW_INFO_COLUMN = 0x100,
  FS_OPTION_SHOW_SAMPLE_COLUMNS = 0x200,
  //Sort items options.
  FS_OPTION_SORT_BY_ID = 0x400,
  FS_OPTION_SORT_BY_NAME = 0x800,
  //This requires the function readdir to be relatively fast because canceling the search will block the GUI.
  FS_OPTION_ALLOW_SEARCH = 0x1000
};

struct connector
{
  const gchar *name;
    gint (*handshake) (struct backend * backend);
  //Used to indicate if the handshake requires a MIDI identity request
  gboolean standard;
  //If the backend device name matches this regex, the handshake will be run before than the connectors that didn't match.
  const gchar *regex;
};

void item_iterator_init (struct item_iterator *iter, const gchar * dir,
			 void *data, iterator_next next, iterator_free free);

gint item_iterator_next (struct item_iterator *iter);

void item_iterator_free (struct item_iterator *iter);

gboolean item_iterator_is_dir_or_matches_extensions (struct item_iterator
						     *iter,
						     const GSList *
						     extensions);

/**
 * Returns the filename for an item, which is a string that uniquely idenfifies an item.
 * In a PC, filenames are typically strings but in embedded devices this could be just a number (in string format).
 * Typically, in these systems, several slots can have the same name but the id is an address to a memory slot.
 * @param options The options member in the fs_operations struct.
 * @param item
 */
gchar *get_filename (guint32 options, struct item *item);

#endif
