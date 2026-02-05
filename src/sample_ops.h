/*
 *   sample_ops.h
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
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

#include "utils.h"

enum sample_ops_zero_crossing_slope
{
  SAMPLE_OPS_ZERO_CROSSING_SLOPE_POSITIVE,
  SAMPLE_OPS_ZERO_CROSSING_SLOPE_NEGATIVE,
  SAMPLE_OPS_ZERO_CROSSING_SLOPE_ANY,
};

guint32 sample_ops_get_next_zero_crossing (struct idata *sample,
					   guint32 frame,
					   enum sample_ops_zero_crossing_slope
					   slope);

guint32 sample_ops_get_prev_zero_crossing (struct idata *sample,
					   guint32 frame,
					   enum sample_ops_zero_crossing_slope
					   slope);

void sample_ops_delete_range (struct idata *sample, guint32 start,
			      guint32 length, gint64 * sel_start,
			      gint64 * len_end);

guint32 sample_ops_detect_start (struct idata *sample);

void sample_ops_normalize (struct idata *sample, guint32 start,
			   guint32 length);
