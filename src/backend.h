/*
 *   backend.h
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

#include <alsa/asoundlib.h>
#include "utils.h"

#ifndef BACKEND_H
#define BACKEND_H

#define BE_FILE_ICON_WAVE "elektroid-wave-symbolic"
#define BE_FILE_ICON_DATA "elektroid-data-symbolic"
#define BE_FILE_ICON_PRJ "elektroid-project-symbolic"
#define BE_FILE_ICON_SND "elektroid-sound-symbolic"

#define BE_MAX_BACKEND_FSS (sizeof (int) * 8)

#define REST_TIME_US 50000
#define SYSEX_TIMEOUT_MS 5000
#define SYSEX_TIMEOUT_GUESS_MS 1000	//If the request might not be implemente, 5 s is too much.
#define SAMPLE_ID_NAME_SEPARATOR ":"

typedef void (*destroy_data) (struct backend *);

struct backend
{
  struct device_desc device_desc;
  snd_rawmidi_t *inputp;
  snd_rawmidi_t *outputp;
  GMutex mutex;
  guint8 *buffer;
  ssize_t rx_len;
  gint npfds;
  struct pollfd *pfds;
  gchar *device_name;
  GHashTable *cache;
  //These must be filled by the concrete backend.
  const struct fs_operations **fs_ops;
  destroy_data destroy_data;
  t_sysex_transfer upgrade_os;
  void *data;
};

struct backend_system_device
{
  gchar *name;
  guint card;
};

gint backend_init (struct backend *, gint);

void backend_destroy (struct backend *);

gint backend_tx_sysex (struct backend *, struct sysex_transfer *);

gint backend_rx_sysex (struct backend *, struct sysex_transfer *);

GByteArray *backend_tx_and_rx_sysex (struct backend *, GByteArray *, gint);

void backend_rx_drain (struct backend *);

gboolean backend_check (struct backend *);

void backend_enable_cache (struct backend *);

void backend_disable_cache (struct backend *);

GArray *backend_get_system_devices ();

const struct fs_operations *backend_get_fs_operations (struct backend *, gint,
						       const char *);

const gchar *backend_get_fs_name (struct backend *, guint);

gchar *connector_get_fs_ext (const struct device_desc *,
			     const struct fs_operations *);

#endif
