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

#define REST_TIME_US 50000
#define SYSEX_TIMEOUT_MS 5000
#define SAMPLE_ID_NAME_SEPARATOR ":"

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

GByteArray *backend_tx_and_rx_sysex (struct backend *, GByteArray *);

void backend_rx_drain (struct backend *);

gboolean backend_check (struct backend *);

GArray *backend_get_system_devices ();

#endif
