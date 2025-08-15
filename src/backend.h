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

#include "utils.h"

#if defined(ELEKTROID_RTMIDI)
#include <fcntl.h>
#include <rtmidi_c.h>
#else
#include <alsa/asoundlib.h>
#endif

#ifndef BACKEND_H
#define BACKEND_H

#define BE_MAX_MIDI_PROGRAMS 128

#define BE_POLL_TIMEOUT_MS 20
#define BE_MAX_TX_LEN KI	//With a higher value than 4 KB, functions behave erratically.
#define BE_INT_BUF_LEN (32 * KI)	//Max length of a SysEx message for Elektroid
#define BE_DEV_RING_BUF_LEN (256 * KI)
#define BE_TMP_BUFF_LEN (64 * KI)	//This size is required by RtMidi as it needs enough space for the messages.

#define BE_REST_TIME_US 50000
#define BE_SYSEX_TIMEOUT_MS 5000
#define BE_SYSEX_TIMEOUT_GUESS_MS 1000	//When the request is not implemented, 5 s is too much.

#define BE_COMPANY_LEN 3
#define BE_FAMILY_LEN 2
#define BE_MODEL_LEN 2
#define BE_VERSION_LEN 4

#define BE_SYSTEM_ID "SYSTEM_ID"

#define BE_SYSEX_EXT "syx"

#define PREF_KEY_STOP_DEVICE_WHEN_CONNECTING "stopDeviceWhenConnecting"

extern GSList *connectors;
extern struct connector *system_connector;

struct backend_storage_stats
{
  gchar name[LABEL_MAX];
  guint64 bsize;
  guint64 bfree;
};

struct backend;

typedef void (*t_destroy_data) (struct backend * backend);

typedef gint (*t_get_storage_stats) (struct backend * backend, guint8 type,
				     struct backend_storage_stats * stats,
				     const gchar * path);

struct backend_midi_info
{
  gchar company[BE_COMPANY_LEN];
  gchar family[BE_FAMILY_LEN];
  gchar model[BE_MODEL_LEN];
  gchar version[BE_VERSION_LEN];
};

enum backend_type
{
  BE_TYPE_NONE,
  BE_TYPE_SYSTEM,
  BE_TYPE_MIDI,
  BE_TYPE_NO_MIDI
};

enum sysex_transfer_status
{
  SYSEX_TRANSFER_STATUS_INIT,
  SYSEX_TRANSFER_STATUS_WAITING,
  SYSEX_TRANSFER_STATUS_SENDING,
  SYSEX_TRANSFER_STATUS_RECEIVING,
  SYSEX_TRANSFER_STATUS_FINISHED
};

struct sysex_transfer
{
  enum sysex_transfer_status status;
  gint timeout;			//Measured in ms. -1 is infinite.
  gint time;
  gboolean batch;
  GByteArray *raw;
  gint err;
};

typedef gint (*t_sysex_transfer) (struct backend *, struct sysex_transfer *,
				  struct controllable * controllable);

struct backend
{
// ALSA or RtMidi backend
#if defined(ELEKTROID_RTMIDI)
  struct RtMidiWrapper *inputp;
  struct RtMidiWrapper *outputp;
#else
  snd_rawmidi_t *inputp;
  snd_rawmidi_t *outputp;
  gint npfds;
  struct pollfd *pfds;
#endif
  guint8 *buffer;
  ssize_t rx_len;
  enum backend_type type;
  struct backend_midi_info midi_info;
  gchar name[LABEL_MAX];
  gchar version[LABEL_MAX];
  gchar description[LABEL_MAX];
  GMutex mutex;
  //This must be filled by the concrete connector.
  const gchar *conn_name;
  GSList *fs_ops;
  void *data;
  t_destroy_data destroy_data;
  t_sysex_transfer upgrade_os;	//This function is device function, not a filesystem function.
  t_get_storage_stats get_storage_stats;	//This function is a device function, not a filesystem function. Several filesystems might share the same memory.
};

struct backend_device
{
  enum backend_type type;
  gchar name[LABEL_MAX];
  gchar id[LABEL_MAX];
};

gint backend_init (struct backend *, struct backend_device *);

void backend_destroy (struct backend *);

ssize_t backend_rx_raw (struct backend *, guint8 *, guint);

ssize_t backend_tx_raw (struct backend *, guint8 *, guint);

gint backend_tx_sysex (struct backend *backend,
		       struct sysex_transfer *sysex_transfer,
		       struct controllable *controllable);

gint backend_rx_sysex (struct backend *backend,
		       struct sysex_transfer *sysex_transfer,
		       struct controllable *controllable);

gint backend_tx (struct backend *, GByteArray *);

GByteArray *backend_rx (struct backend *backend, gint timeout,
			struct controllable *controllable);

gint backend_tx_and_rx_sysex_transfer (struct backend *backend,
				       struct sysex_transfer *sysex_transfer,
				       struct controllable *controllable);

GByteArray *backend_tx_and_rx_sysex (struct backend *, GByteArray *, gint);

void backend_rx_drain (struct backend *);

gboolean backend_check (struct backend *);

GArray *backend_get_devices ();

const struct fs_operations *backend_get_fs_operations_by_id (struct backend *,
							     guint32);

const struct fs_operations *backend_get_fs_operations_by_name (struct backend
							       *,
							       const char *);

const gchar *backend_get_fs_name (struct backend *, guint);

gdouble backend_get_storage_stats_percent (struct backend_storage_stats *);

void backend_destroy_data (struct backend *);

void backend_midi_handshake (struct backend *backend);

gint backend_program_change (struct backend *, guint8, guint8);

gint backend_send_controller (struct backend *backend, guint8 channel,
			      guint8 controller, guint8 value);

gint backend_send_note_on (struct backend *backend, guint8 channel,
			   guint8 note, guint8 velocity);

gint backend_send_note_off (struct backend *backend, guint8 channel,
			    guint8 note, guint8 velocity);

gint backend_send_rpn (struct backend *backend, guint8 channel,
		       guint8 controller_msb, guint8 controller_lsb,
		       guint8 value_msb, guint8 value_lsb);

/**
 * Returns a human readable message for the given error or for the last ocurred error if the underlying API only returs a boolean value.
 */

const gchar *backend_strerror (struct backend *backend, gint error);

enum path_type backend_get_path_type (struct backend *);

const gchar *backend_name ();

void backend_fill_fs_ops (struct backend *backend, ...);

gint backend_init_connector (struct backend *backend,
			     struct backend_device *device,
			     const gchar * name,
			     struct controllable *controllable);

enum sysex_transfer_status
sysex_transfer_get_status (struct sysex_transfer *sysex_transfer,
			   struct controllable *controllable);

void sysex_transfer_init_tx (struct sysex_transfer *sysex_transfer,
			     GByteArray * data);

void sysex_transfer_init_rx (struct sysex_transfer *sysex_transfer,
			     gint timeout, gboolean batch);

void sysex_transfer_init_tx_and_rx (struct sysex_transfer *sysex_transfer,
				    gint timeout, GByteArray * data);

GByteArray *sysex_transfer_steal (struct sysex_transfer *sysex_transfer);

void sysex_transfer_free (struct sysex_transfer *sysex_transfer);

#endif
