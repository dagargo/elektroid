/*
 *   connector.h
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
#include <alsa/asoundlib.h>
#include "utils.h"

#define SYSEX_TIMEOUT 5000

struct connector_device_desc
{
  const gchar *name;
  const gchar *alias;
  guint8 id;
  guint8 fss;
  guint8 storages;
};

struct connector
{
  const struct connector_device_desc *device_desc;
  snd_rawmidi_t *inputp;
  snd_rawmidi_t *outputp;
  gchar *device_name;
  gushort seq;
  GMutex mutex;
  ssize_t rx_len;
  guint8 *buffer;
  gint npfds;
  struct pollfd *pfds;
};

struct connector_iterator_data
{
  GByteArray *msg;
  guint32 pos;
  guint32 cksum;
  guint16 operations;
  guint8 has_valid_data;
  guint8 has_metadata;
};

struct connector_system_device
{
  gchar *name;
  guint card;
};

enum connector_sysex_transfer_status
{
  WAITING,
  SENDING,
  RECEIVING,
  FINISHED
};

struct connector_sysex_transfer
{
  gboolean active;
  enum connector_sysex_transfer_status status;
  gint timeout;			//Measured in ms. -1 is infinite.
  gboolean batch;
  GMutex mutex;
};

enum connector_storage
{
  STORAGE_PLUS_DRIVE = 0x1,
  STORAGE_RAM = 0x2
};

struct connector_storage_stats
{
  const gchar *name;
  guint64 bsize;
  guint64 bfree;
};

enum connector_fs
{
  FS_NONE = 0,
  FS_SAMPLES = 0x1,
  FS_DATA = 0x2
};

const struct fs_operations *connector_get_fs_operations (enum connector_fs);

gint connector_init (struct connector *, gint);

void connector_destroy (struct connector *);

gboolean connector_check (struct connector *);

GArray *connector_get_system_devices ();

ssize_t connector_tx_sysex (struct connector *, GByteArray *,
			    struct connector_sysex_transfer *);

GByteArray *connector_rx_sysex (struct connector *,
				struct connector_sysex_transfer *);


void connector_rx_drain (struct connector *);

gint connector_upgrade_os (struct connector *, GByteArray *,
			   struct connector_sysex_transfer *);

gint connector_get_storage_stats (struct connector *,
				  enum connector_storage,
				  struct connector_storage_stats *);

gdouble connector_get_storage_stats_percent (struct connector_storage_stats
					     *);

gchar *connector_get_upload_path (struct item_iterator *,
				  const struct fs_operations *, const gchar *,
				  const gchar *, gint32 *);

gchar *connector_get_download_path (const struct connector_device_desc *,
				    struct item_iterator *,
				    const struct fs_operations *,
				    const gchar *, const gchar *);

gchar *connector_get_full_ext (const struct connector_device_desc *,
			       const struct fs_operations *);
