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

#define SYSEX_TIMEOUT 5000

#define CAP_SAMPLE  0b00000001

struct connector_device_desc
{
  guint8 id;
  gchar *model;
  guint8 capabilities;
  guint8 startup_mode;
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

struct connector_entry_iterator;

typedef guint (*connector_entry_iterator) (struct connector_entry_iterator *);

struct connector_entry_iterator
{
  gchar *entry;
  gchar type;
  guint32 size;
  guint32 cksum;
  GByteArray *msg;
  guint32 pos;
  guint16 operations;
  guint8 has_metadata;
  connector_entry_iterator iterator;
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

struct connector_sample_transfer
{
  gboolean active;
  GMutex mutex;
};

enum connector_fs_type
{
  FS_NONE,
  FS_PLUS_DRIVE,
  FS_RAM
};

struct connector_statfs
{
  const gchar *name;
  guint64 bsize;
  guint64 bfree;
};

typedef guint (*connector_read_items) (struct connector *, const gchar *);
typedef gint (*connector_create_dir) (struct connector *, const gchar *);
typedef gint (*connector_delete_file) (struct connector *, const gchar *);
typedef gint (*connector_delete_dir) (struct connector *, const gchar *);
typedef gint (*connector_rename) (struct connector *, const gchar *,
				  const gchar *);
typedef GArray *(*connector_download) (struct connector *, const gchar *,
				       struct connector_sample_transfer *,
				       void (*)(gdouble));
typedef ssize_t (*connector_upload) (struct connector *, GArray *, gchar *,
				     struct connector_sample_transfer *,
				     void (*)(gdouble));

struct connector_type_operations
{
  connector_read_items read_items;
  connector_create_dir create_dir;
  connector_delete_dir delete_dir;
  connector_delete_file delete_file;
  connector_rename rename;
  connector_download download;
  connector_upload upload;
};

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

gint connector_statfs (struct connector *, enum connector_fs_type,
		       struct connector_statfs *);

float connector_statfs_use_percent (struct connector_statfs *);

void connector_free_iterator (struct connector_entry_iterator *);

guint connector_next_entry (struct connector_entry_iterator *);

struct connector_entry_iterator *connector_read_samples (struct connector *,
							 const gchar *);

gint connector_create_samples_dir (struct connector *, const gchar *);

gint connector_delete_samples_dir (struct connector *, const gchar *);

gint connector_delete_sample (struct connector *, const gchar *);

gint connector_rename_sample (struct connector *, const gchar *,
			      const gchar *);

GArray *connector_download_sample (struct connector *, const gchar *,
				   struct connector_sample_transfer *,
				   void (*)(gdouble));

ssize_t
connector_upload_sample (struct connector *, GArray *, gchar *,
			 struct connector_sample_transfer *,
			 void (*)(gdouble));

struct connector_entry_iterator *connector_read_data (struct connector *,
						      const gchar *);
