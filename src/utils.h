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

#include <stdio.h>
#include <glib.h>

#ifndef UTILS_H
#define UTILS_H

#define LABEL_MAX 128

#define debug_print(level, format, ...) if (level <= debug_level) fprintf(stderr, "DEBUG:" __FILE__ ":%d:(%s): " format, __LINE__, __FUNCTION__, ## __VA_ARGS__)
#define error_print(format, ...) fprintf(stderr, "\x1b[31mERROR:" __FILE__ ":%d:(%s): " format "\x1b[m", __LINE__, __FUNCTION__, ## __VA_ARGS__)

enum item_type
{
  ELEKTROID_NONE = 0,
  ELEKTROID_FILE = 'F',
  ELEKTROID_DIR = 'D'
};

struct item
{
  gchar *name;
  gint size;
  gint index;
  enum item_type type;
};

struct item_iterator;

typedef guint (*fs_iterator_next) (struct item_iterator *);
typedef void (*fs_iterator_free) (void *);

struct item_iterator
{
  fs_iterator_next next;
  fs_iterator_free free;
  void *data;
  gchar *entry;
  gchar type;
  guint32 size;
  gint32 id;
};

extern int debug_level;

typedef gchar *(*get_item_id) (struct item *);

gchar *debug_get_hex_data (gint, guint8 *, guint);

gchar *debug_get_hex_msg (const GByteArray *);

gchar *chain_path (const gchar *, const gchar *);

void remove_ext (gchar *);

const gchar *get_ext (const gchar *);

gchar get_type_from_inventory_icon (const gchar *);

gchar *get_local_startup_path (const gchar *);

void free_msg (gpointer);

gchar *get_item_name (struct item *);

gchar *get_item_index (struct item *);

guint next_item_iterator (struct item_iterator *);

void free_item_iterator (struct item_iterator *);

#endif
