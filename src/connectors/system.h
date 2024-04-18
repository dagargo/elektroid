/*
 *   system.h
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
#include "utils.h"

gint system_read_dir (struct backend *, struct item_iterator *, const gchar *,
		      gchar **);

gint system_samples_read_dir (struct backend *, struct item_iterator *,
			      const gchar *, gchar **);

gboolean system_file_exists (struct backend *, const gchar *);

gint system_mkdir (struct backend *, const gchar *);

gint system_delete (struct backend *, const gchar *);

gint system_rename (struct backend *, const gchar *, const gchar *);

gint system_upload (struct backend *, const gchar *, GByteArray *,
		    struct job_control *);

gint system_init_backend (struct backend *, const gchar *);
