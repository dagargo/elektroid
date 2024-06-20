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

#define SAMPLE_SIZE(format) ((format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16 ? 2 : 4)
#define FRAME_SIZE(channels,format) ((channels) * SAMPLE_SIZE(format))
#define SAMPLE_INFO_FRAME_SIZE(sample_info) ((sample_info)->channels * SAMPLE_SIZE((sample_info)->format))
#define MONO_MIX_GAIN(channels) (channels == 2 ? 0.5 : 1.0 / sqrt (channels))

typedef void (*sample_load_cb) (struct job_control * control,
				gdouble progress, gpointer data);

gint sample_save_to_file (const gchar * path, struct idata *sample,
			  struct job_control *control, guint32 format);

gint sample_load_from_memfile (struct idata *memfile, struct idata *sample,
			       struct job_control *control,
			       struct sample_info *sample_info);

gint sample_load_from_file (const gchar * path, struct idata *sample,
			    struct job_control *control,
			    struct sample_info *sample_info);

gint sample_get_memfile_from_sample (struct idata *sample,
				     struct idata *file,
				     struct job_control *control,
				     guint32 format);

gint sample_load_from_file_with_cb (const gchar * path, struct idata *sample,
				    struct job_control *control,
				    struct sample_info *sample_info,
				    sample_load_cb callback, gpointer data);

gint sample_load_sample_info (const gchar * path,
			      struct sample_info *sample_info);

GSList *sample_get_sample_extensions ();

void sample_check_and_fix_loop_points (struct sample_info *sample_info);

const gchar *sample_get_format (struct sample_info *sample_info);

const gchar *sample_get_subtype (struct sample_info *sample_info);

gint sample_resample (GByteArray * input, GByteArray * output, gint channels,
		      gdouble ratio);

#endif
