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

#ifndef SDS_H
#define SDS_H

#include "connector.h"

extern const struct connector CONNECTOR_SDS;

struct sds_data
{
  gint rest_time;
  gboolean name_extension;
};

struct sds_iterator_data
{
  guint32 next;
  guint32 last;
  struct backend *backend;
};

gint sds_download_by_id (struct backend *backend, guint id,
			 struct idata *sample, struct job_control *control);

gint sds_download (struct backend *backend, const gchar * path,
		   struct idata *sample, struct job_control *control);

gint sds_upload_by_id_name (struct backend *backend, guint id,
			    struct idata *sample, struct job_control *control,
			    guint bits);

gint sds_upload (struct backend *backend, const gchar * path,
		 struct idata *sample, struct job_control *control,
		 guint bits);

gint sds_sample_load (const gchar * path, struct idata *sample,
		      struct job_control *control);

gint sds_sample_save (const gchar * path, struct idata *sample,
		      struct job_control *control);

gint sds_rename_by_id (struct backend *backend, guint id, const gchar * dst);

gint sds_rename (struct backend *backend, const gchar * src,
		 const gchar * dst);

gint sds_next_sample_dentry (struct item_iterator *);

#endif
