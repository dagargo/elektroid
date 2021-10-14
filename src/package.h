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

#define PKG_FILE_TYPE_SOUND   0x01
#define PKG_FILE_TYPE_PROJECT 0x02
#define PKG_FILE_WITH_SAMPLES 0x10

enum package_resource_type
{
  PKG_RES_TYPE_NONE,
  PKG_RES_TYPE_MAIN,
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

struct package
{
  gchar *name;
  const gchar *fw_version;
  guint8 product;
  guint8 type;
  gchar *buff;
  zip_source_t *zip_source;
  zip_t *zip;
  GList *resources;
  struct package_resource *manifest;
};

gint package_begin (struct package *, gchar *, const gchar *, guint8, guint8);

void package_free_package_resource (gpointer);

gint package_add_resource (struct package *, struct package_resource *);

gint package_end (struct package *, GByteArray *);

void package_destroy (struct package *);
