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

#define CONF_DIR "/.config/" PACKAGE

#define APP_NAME "Elektroid"

//Used to identify the local topmost directory.
//As they are different and the TOPMOST_DIR_WINDOWS is not a "real" directory, it can be used as a special case.
#define TOPMOST_DIR_UNIX "/"
#define TOPMOST_DIR_WINDOWS ""

#define LABEL_MAX 256

#define KI 1024
#define MI (KI * KI)
#define GI (KI * MI)

#define debug_print(level, format, ...) {\
  if (level <= debug_level) \
    { \
      fprintf(stderr, "DEBUG:" __FILE__ ":%d:%s: " format "\n", __LINE__, __FUNCTION__, ## __VA_ARGS__); \
    } \
}

#define error_print(format, ...) { \
  gboolean tty = isatty(fileno(stderr)); \
  fprintf(stderr, "%sERROR:" __FILE__ ":%d:%s: " format "%s\n", tty ? "\x1b[31m" : "", __LINE__, __FUNCTION__, ## __VA_ARGS__, tty ? "\x1b[m" : ""); \
}

struct sample_info
{
  guint32 frames;
  guint32 loop_start;
  guint32 loop_end;
  guint32 loop_type;		// 0 = forward loop
  guint32 rate;
  guint64 format;		// Use 32 lower bits as in libsndfile and upper 32 bits for extended file formats.
  guint32 channels;
  guint32 midi_note;
  guint32 midi_fraction;
};

struct job_control;

typedef void (*job_control_callback) (struct job_control *);
typedef void (*job_control_progress_callback) (struct job_control *, gdouble);

struct controllable
{
  gboolean active;
  GMutex mutex;
};

struct job_control
{
  struct controllable controllable;
  GCond cond;			//This can be used by the calling threads. It requires to call g_cond_init and g_cond_clear.
  job_control_callback callback;
  gint parts;
  gint part;
  gdouble progress;
};

enum path_type
{
  PATH_INTERNAL,		// Slash separated paths
  PATH_SYSTEM			// Slash or backslash depending on the system
};

struct idata
{
  GByteArray *content;
  gchar *name;			//Optional field to store a name
  void *info;			//Optional field to store information about the content
};

extern int debug_level;

gchar *debug_get_hex_data (gint, guint8 *, guint);

gchar *debug_get_hex_msg (const GByteArray *);

void filename_remove_ext (gchar *);

const gchar *filename_get_ext (const gchar *);

gint filename_get_lenght_without_ext (const gchar * name);

gchar *get_user_dir (const gchar *);

gchar *get_system_startup_path (const gchar *);

void free_msg (gpointer);

gint file_load (const char *path, struct idata *idata,
		struct job_control *control);

gint file_save (const char *path, struct idata *idata,
		struct job_control *control);

gint file_save_data (const gchar * path, const guint8 * data, ssize_t len);

gchar *get_human_size (gint64, gboolean);

void job_control_set_progress_no_sync (struct job_control *control,
				       gdouble p);

void job_control_set_progress (struct job_control *control, gdouble progress);

gboolean filename_matches_exts (const gchar * name,
				const gchar ** extensions);

gboolean filename_is_dir_or_matches_exts (const gchar * name,
					  const gchar ** exts);

gchar *path_chain (enum path_type, const gchar *, const gchar *);

gchar *path_translate (enum path_type, const gchar *);

gchar *path_filename_from_uri (enum path_type, gchar *);

gchar *path_filename_to_uri (enum path_type, gchar *);

void gslist_fill (GSList ** list, ...);

void idata_init (struct idata *idata, GByteArray * content,
		 gchar * name, void *info);

void idata_free (struct idata *idata);

GByteArray *idata_steal (struct idata *idata);

void job_control_reset (struct job_control *control, gint parts);

guint32 cents_to_midi_fraction (guint32);

guint32 midi_fraction_to_cents (guint32);

void controllable_init (struct controllable *controllable);

void controllable_clear (struct controllable *controllable);

void controllable_set_active (struct controllable *controllable,
			      gboolean active);

gboolean controllable_is_active (struct controllable *controllable);

#endif
