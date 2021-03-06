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

#define LABEL_MAX 128

#define ELEKTROID_FILE 'F'
#define ELEKTROID_DIR 'D'
#define ELEKTROID_NONE '?'

#define debug_print(level, format, ...) if (level <= debug_level) fprintf(stderr, "DEBUG:" __FILE__ ":%d:(%s): " format, __LINE__, __FUNCTION__, ## __VA_ARGS__)
#define error_print(format, ...) fprintf(stderr, "\x1b[31mERROR:" __FILE__ ":%d:(%s): " format "\x1b[m", __LINE__, __FUNCTION__, ## __VA_ARGS__)

extern int debug_level;

gchar *debug_get_hex_data (gint, guint8 *, guint);

gchar *debug_get_hex_msg (const GByteArray *);

gchar *chain_path (const gchar *, const gchar *);

void remove_ext (gchar *);

const gchar *get_ext (const gchar *);

gchar get_type_from_inventory_icon (const gchar *);

const gchar *get_inventory_icon_from_type (gchar);

gchar *get_local_startup_path (const gchar *);
