/*
 *   sds.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#ifndef SDS_H
#define SDS_H

gint sds_handshake (struct backend *);

gchar *sds_get_upload_path (struct backend *, struct item_iterator *,
			    const struct fs_operations *,
			    const gchar *, const gchar *, gint32 *);

gchar *sds_get_download_path (struct backend *, struct item_iterator *,
			      const struct fs_operations *,
			      const gchar *, const gchar *);

gint sds_download (struct backend *, const gchar *, GByteArray *,
		   struct job_control *);

gint sds_upload (struct backend *, const gchar *, GByteArray *,
		 struct job_control *);

gint sds_read_dir (struct backend *, struct item_iterator *, const gchar *);

gint sds_sample_load (const gchar *, GByteArray *, struct job_control *);

#endif
