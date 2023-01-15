/*
 *   package.h
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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
#include <zip.h>
#include "backend.h"

#ifndef PACKAGE_H
#define PACKAGE_H

#define ELEKTRON_SAMPLE_RATE 48000
#define ELEKTRON_SAMPLE_CHANNELS 1

enum package_resource_type
{
  PKG_RES_TYPE_NONE,
  PKG_RES_TYPE_PAYLOAD,
  PKG_RES_TYPE_MANIFEST,
  PKG_RES_TYPE_SAMPLE
};

struct package_resource
{
  enum package_resource_type type;
  guint32 hash;
  guint32 size;
  gchar *path;
  GByteArray *data;
};

enum package_type
{
  PKG_FILE_TYPE_NONE,
  PKG_FILE_TYPE_SOUND,
  PKG_FILE_TYPE_PROJECT,
  PKG_FILE_TYPE_PRESET,
};

struct device_desc
{
  guint32 id;
  gchar name[LABEL_MAX];
  gchar alias[LABEL_MAX];
};

struct package
{
  gchar *name;
  enum package_type type;
  gchar *fw_version;
  const struct device_desc *device_desc;
  gchar *buff;
  zip_source_t *zip_source;
  zip_t *zip;
  GList *resources;
  struct package_resource *manifest;
};

gint package_begin (struct package *, gchar *, const gchar *,
		    const struct device_desc *, enum package_type);

gint package_receive_pkg_resources (struct package *, const gchar *,
				    struct job_control *, struct backend *,
				    fs_remote_file_op, fs_remote_file_op);

gint package_end (struct package *, GByteArray *);

void package_destroy (struct package *);

gint package_open (struct package *, GByteArray *,
		   const struct device_desc *);

gint package_send_pkg_resources (struct package *, const gchar *,
				 struct job_control *, struct backend *,
				 fs_remote_file_op);

void package_close (struct package *);

extern const struct sample_params ELEKTRON_SAMPLE_PARAMS;

#endif
