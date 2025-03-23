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
  guint32 format;		// Used as in libsndfile.
  guint32 channels;
  guint32 midi_note;
};

struct job_control;

typedef void (*job_control_callback) (struct job_control *);

struct job_control
{
  gboolean active;
  GMutex mutex;
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
				       gdouble progress);

void job_control_set_progress (struct job_control *control, gdouble progress);

gboolean file_matches_extensions (const gchar * name,
				  const gchar ** extensions);

gchar *path_chain (enum path_type, const gchar *, const gchar *);

gchar *path_translate (enum path_type, const gchar *);

gchar *path_filename_from_uri (enum path_type, gchar *);

gchar *path_filename_to_uri (enum path_type, gchar *);

void gslist_fill (GSList ** list, ...);

void idata_init (struct idata *idata, GByteArray * content,
		 gchar * name, void *info);

void idata_free (struct idata *idata);

GByteArray *idata_steal (struct idata *idata);

gboolean job_control_get_active_lock (struct job_control *control);

void job_control_set_active_lock (struct job_control *control,
				  gboolean active);

void job_control_reset (struct job_control *control, gint parts);

#endif
