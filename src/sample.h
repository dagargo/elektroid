/*
 *   sample.h
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

#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sndfile.h>
#include "utils.h"

#ifndef SAMPLE_H
#define SAMPLE_H

#define SF_FORMAT_PCM_S8_STR "s8"
#define SF_FORMAT_PCM_16_STR "s16"
#define SF_FORMAT_PCM_24_STR "s24"
#define SF_FORMAT_PCM_32_STR "s32"
#define SF_FORMAT_PCM_U8_STR "u8"
#define SF_FORMAT_FLOAT_STR "f32"
#define SF_FORMAT_DOUBLE_STR "f64"
#define SF_FORMAT_UNKNOWN "?"

#define ELEKTROID_SAMPLE_FORMAT_MASK       0xffffffff00000000
#define ELEKTROID_SAMPLE_FORMAT_MICROFREAK 0x0000000100000000

#define SAMPLE_SIZE(format) ((format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16 ? 2 : 4)
#define FRAME_SIZE(channels,format) ((channels) * SAMPLE_SIZE(format))
#define SAMPLE_INFO_FRAME_SIZE(sample_info) FRAME_SIZE((sample_info)->channels, (sample_info)->format)
#define MONO_MIX_GAIN(channels) (channels == 2 ? 0.5 : 1.0 / sqrt (channels))

struct sample_load_opts
{
  guint32 channels;
  guint32 rate;
  guint32 format;		// Used as in libsndfile
  gboolean tags;
};

struct backend;
struct fs_operations;

void task_control_set_sample_progress (struct task_control *control,
				       gdouble p);

gint sample_save_to_file (const gchar * path, struct idata *sample,
			  struct task_control *control, guint32 format);

gint sample_load_from_memfile (struct idata *memfile, struct idata *sample,
			       struct task_control *control,
			       const struct sample_load_opts
			       *sample_load_opts,
			       struct sample_info *sample_info_src);

gint sample_load_from_file (const gchar * path, struct idata *sample,
			    struct task_control *control,
			    const struct sample_load_opts *sample_load_opts,
			    struct sample_info *sample_info_src);

gint sample_get_memfile_from_sample (struct idata *sample,
				     struct idata *file,
				     struct task_control *control,
				     guint32 format);

gint sample_load_from_file_full (const gchar * path, struct idata *sample,
				 struct task_control *control,
				 const struct sample_load_opts
				 *sample_load_opts,
				 struct sample_info *sample_info_src,
				 task_control_progress_callback cb);

gint sample_load_sample_info (const gchar * path,
			      struct sample_info *sample_info);

const gchar **sample_get_sample_extensions (struct backend *backend,
					    const struct fs_operations *ops);

void sample_check_and_fix_loop_points (struct sample_info *sample_info);

const gchar *sample_get_format (struct sample_info *sample_info);

const gchar *sample_get_subtype (struct sample_info *sample_info);

gint sample_reload (struct idata *input, struct idata *output,
		    struct task_control *control,
		    const struct sample_load_opts *sample_load_opts,
		    task_control_progress_callback cb);

guint32 sample_get_internal_format ();

void sample_load_opts_init (struct sample_load_opts *opts, guint32 channels,
			    guint32 rate, guint32 format, gboolean tags);

void sample_load_opts_init_direct (struct sample_load_opts *opts,
				   gboolean tags);

void sample_load_opts_init_from_sample_info (struct sample_load_opts *opts,
					     struct sample_info *sample_info,
					     gboolean tags);

gboolean sample_format_is_valid_to_save (struct sample_info *sample_info);

void sample_format_set_to_save (struct sample_info *sample_info);

#endif
