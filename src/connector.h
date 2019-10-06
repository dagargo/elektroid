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

struct connector
{
  snd_rawmidi_t *inputp;
  snd_rawmidi_t *outputp;
  gchar *device_name;
  gushort seq;
};

struct connector_dir_iterator
{
  gchar *dentry;
  gchar type;
  guint size;
  const GByteArray *dir_payload;
  guint position;
};

typedef struct connector_device
{
  gchar *name;
  guint card;
} ConnectorDevice;

int connector_init (struct connector *, gint);

void connector_destroy (struct connector *);

int connector_check (struct connector *);

struct connector_dir_iterator *connector_new_dir_iterator (const GByteArray
							   *);

guint connector_get_next_dentry (struct connector_dir_iterator *);

ssize_t connector_tx (struct connector *, const GByteArray *);

GByteArray *connector_rx (struct connector *);

GByteArray *connector_new_msg_dir_list (const gchar *);

GByteArray *connector_new_msg_info_file (const gchar *);

GByteArray *connector_new_msg_new_dir (const gchar *);

GByteArray *connector_new_msg_new_upload (const gchar *, guint);

GArray *connector_download (struct connector *, const char *, int *,
			    void (*)(gdouble));

ssize_t
connector_upload (struct connector *, GArray *, guint, int *,
		  void (*)(gdouble));

void connector_get_sample_info_from_msg (GByteArray *, guint *, guint *);

gint connector_create_upload (struct connector *, char *, guint);

gint connector_create_dir (struct connector *, char *);

GArray *connector_get_elektron_devices ();
