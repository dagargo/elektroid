/*
 *   microfreak_sample.h
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include "sample.h"

#ifndef MICROFREAK_UTILS_H
#define MICROFREAK_UTILS_H

#define MICROFREAK_SAMPLERATE 32000
#define MICROFREAK_SAMPLE_SIZE (sizeof(gint16))
#define MICROFREAK_WAVETABLE_SAMPLE_LEN 256
#define MICROFREAK_WAVETABLE_CYCLES 32
#define MICROFREAK_WAVETABLE_LEN (MICROFREAK_WAVETABLE_SAMPLE_LEN * MICROFREAK_WAVETABLE_CYCLES)
#define MICROFREAK_WAVETABLE_SIZE (MICROFREAK_WAVETABLE_LEN * MICROFREAK_SAMPLE_SIZE)

#define MICROFREAK_WAVETABLE_NAME_LEN 16

#define MICROFREAK_PWAVETABLE_EXT "mfw"
#define MICROFREAK_ZWAVETABLE_EXT "mfwz"

#define MICROFREAK_PSAMPLE_EXT "mfs"
#define MICROFREAK_ZSAMPLE_EXT "mfsz"

gint microfreak_serialize_object (GByteArray * output, const gchar * header,
				  const gchar * name, guint8 p0, guint8 p3,
				  guint8 p5, guint8 * data, guint datalen);

gint microfreak_deserialize_object (GByteArray * input, const gchar * header,
				    gchar * name, guint8 * p0, guint8 * p3,
				    guint8 * p5, guint8 * data,
				    gint64 * datalen);

struct sample_info *microfreak_new_sample_info (guint32 frames);

gint microfreak_zobject_save (const gchar * path, struct idata *zobject,
			      struct task_control *control,
			      const gchar * name);

gint microfreak_zobject_load (const char *path, struct idata *zobject,
			      struct task_control *control);

gint microfreak_zsample_load (const gchar * path, struct idata *sample,
			      struct task_control *control);

gint microfreak_psample_load (const gchar * path, struct idata *sample,
			      struct task_control *control);

gint microfreak_pwavetable_load (const gchar * path, struct idata *wavetable,
				 struct task_control *control);

gint microfreak_zwavetable_load (const gchar * path, struct idata *wavetable,
				 struct task_control *control);

gint microfreak_pwavetable_save (const gchar * path, struct idata *wavetable,
				 struct task_control *control);

gint microfreak_zwavetable_save (const gchar * path, struct idata *wavetable,
				 struct task_control *control);

#endif
