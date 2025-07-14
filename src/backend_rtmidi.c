/*
 *   backend_rtmidi.c
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

#include <rtmidi_c.h>
#include "backend.h"

#if defined(__linux__)
#define ELEKTROID_RTMIDI_API RTMIDI_API_LINUX_ALSA
#define FIRST_OUTPUT_PORT 1	//Skip Midi Through
#elif defined(__APPLE__) && defined(__MACH__)
#define ELEKTROID_RTMIDI_API RTMIDI_API_MACOSX_CORE
#define FIRST_OUTPUT_PORT 0
#else
#define ELEKTROID_RTMIDI_API RTMIDI_API_WINDOWS_MM
#define FIRST_OUTPUT_PORT 1	//Skip Microsoft GS Wavetable Synth 0
#endif

#define INPUT_OUTPUT_SEPARATOR " :: "

void
sysex_transfer_set_status (struct sysex_transfer *sysex_transfer,
			   struct controllable *controllable,
			   enum sysex_transfer_status status);

void
backend_destroy_int (struct backend *backend)
{
  if (backend->inputp)
    {
      rtmidi_close_port (backend->inputp);
      rtmidi_in_free (backend->inputp);
      backend->inputp = NULL;
    }
  if (backend->outputp)
    {
      rtmidi_close_port (backend->outputp);
      rtmidi_in_free (backend->outputp);
      backend->outputp = NULL;
    }
  if (backend->buffer)
    {
      g_free (backend->buffer);
      backend->buffer = NULL;
    }
}

gint
backend_init_int (struct backend *backend, const gchar *id)
{
  struct RtMidiWrapper *inputp;
  struct RtMidiWrapper *outputp;
  guint iports, oports, err = 0;
  gchar iportname[LABEL_MAX];
  gchar oportname[LABEL_MAX];
  gint iportnamelen, oportnamelen;

  backend->inputp = NULL;
  backend->outputp = NULL;
  backend->buffer = NULL;

  if (!(inputp = rtmidi_in_create_default ()))
    {
      return -ENODEV;
    }

  if (!(outputp = rtmidi_out_create_default ()))
    {
      err = -ENODEV;
      goto cleanup_input;
    }

  iports = rtmidi_get_port_count (inputp);
  oports = rtmidi_get_port_count (outputp);
  for (guint i = 0; i < iports; i++)
    {
      if (rtmidi_get_port_name (inputp, i, NULL, &iportnamelen))
	{
	  goto cleanup_output;
	}
      if (rtmidi_get_port_name (inputp, i, iportname, &iportnamelen) < 0)
	{
	  goto cleanup_output;
	}
      for (guint j = FIRST_OUTPUT_PORT; j < oports; j++)
	{
	  if (rtmidi_get_port_name (outputp, j, NULL, &oportnamelen))
	    {
	      goto cleanup_output;
	    }
	  if (rtmidi_get_port_name (outputp, j, oportname, &oportnamelen) < 0)
	    {
	      goto cleanup_output;
	    }
#if defined(__linux__)
	  if (!strcmp (iportname, oportname) && !strcmp (iportname, id))
#else
	  guint iportnamelen = strlen (iportname);
	  if (!strncmp (id, iportname, iportnamelen) &&
	      !strcmp (id + iportnamelen +
		       strlen (INPUT_OUTPUT_SEPARATOR), oportname))
#endif
	    {
	      backend->inputp = rtmidi_in_create (ELEKTROID_RTMIDI_API,
						  PACKAGE_NAME,
						  BE_INT_BUF_LEN);
	      rtmidi_in_ignore_types (backend->inputp, false, true, true);
	      rtmidi_open_port (backend->inputp, i, PACKAGE_NAME);
	      backend->outputp =
		rtmidi_out_create (ELEKTROID_RTMIDI_API, PACKAGE_NAME);
	      rtmidi_open_port (backend->outputp, j, PACKAGE_NAME);
	      backend->rx_len = 0;

	      backend->buffer = g_malloc (sizeof (guint8) * BE_INT_BUF_LEN);
	      goto cleanup_output;
	    }
	}
    }

cleanup_output:
  rtmidi_close_port (inputp);
  rtmidi_in_free (inputp);

cleanup_input:
  rtmidi_close_port (outputp);
  rtmidi_out_free (outputp);

  return err;
}

gint
backend_tx_sysex_int (struct backend *backend,
		      struct sysex_transfer *transfer,
		      struct controllable *controllable)
{
  transfer->err = 0;
  sysex_transfer_set_status (transfer, controllable,
			     SYSEX_TRANSFER_STATUS_SENDING);

  rtmidi_out_send_message (backend->outputp, transfer->raw->data,
			   transfer->raw->len);
  transfer->err = backend->outputp->ok ? 0 : -EIO;

  if (!transfer->err && debug_level >= 2)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message sent (%d): %s", transfer->raw->len, text);
      g_free (text);
    }

  sysex_transfer_set_status (transfer, controllable,
			     SYSEX_TRANSFER_STATUS_FINISHED);

  return transfer->err;
}

ssize_t
backend_tx_raw (struct backend *backend, guint8 *data, guint len)
{
  struct sysex_transfer transfer;
  transfer.raw = g_byte_array_sized_new (len);
  g_byte_array_append (transfer.raw, data, len);
  backend_tx_sysex_int (backend, &transfer, NULL);
  return transfer.err ? transfer.err : len;
}

void
backend_rx_drain_int (struct backend *backend)
{
  while (1)
    {
      size_t len = BE_INT_BUF_LEN;
      rtmidi_in_get_message (backend->inputp, backend->buffer, &len);
      if (len == 0)
	{
	  break;
	}
    }
}

ssize_t
backend_rx_raw (struct backend *backend, guint8 *buffer, guint len)
{
  size_t size = len;
  rtmidi_in_get_message (backend->inputp, buffer, &size);
  if (!backend->inputp->ok)
    {
      return -EIO;
    }

  if (!size)
    {
      usleep (BE_POLL_TIMEOUT_MS * 1000);
    }

  return size;
}

gboolean
backend_check_int (struct backend *backend)
{
  return backend->inputp && backend->outputp;
}

void
backend_fill_devices_array (GArray *devices)
{
  struct RtMidiWrapper *inputp;
  struct RtMidiWrapper *outputp;
  guint iports, oports;
  gchar iportname[LABEL_MAX];
  gchar oportname[LABEL_MAX];
  gint iportnamelen, oportnamelen;
  struct backend_device *backend_device;

  if (!(inputp = rtmidi_in_create_default ()))
    {
      return;
    }

  if (!(outputp = rtmidi_out_create_default ()))
    {
      goto cleanup_input;
    }

  iports = rtmidi_get_port_count (inputp);
  oports = rtmidi_get_port_count (outputp);
  for (guint i = 0; i < iports; i++)
    {
      if (rtmidi_get_port_name (inputp, i, NULL, &iportnamelen))
	{
	  goto cleanup_output;
	}
      if (rtmidi_get_port_name (inputp, i, iportname, &iportnamelen) < 0)
	{
	  goto cleanup_output;
	}
      for (guint j = FIRST_OUTPUT_PORT; j < oports; j++)
	{
	  if (rtmidi_get_port_name (outputp, j, NULL, &oportnamelen))
	    {
	      goto cleanup_output;
	    }
	  if (rtmidi_get_port_name (outputp, j, oportname, &oportnamelen) < 0)
	    {
	      goto cleanup_output;
	    }
	  debug_print (3, "Checking I/O availability (%s == %s)...",
		       iportname, oportname);
#if defined(__linux__)
	  if (!strcmp (iportname, oportname))
	    {
	      backend_device = g_malloc (sizeof (struct backend_device));
	      backend_device->type = BE_TYPE_MIDI;
	      snprintf (backend_device->id, LABEL_MAX, "%s", iportname);
	      snprintf (backend_device->name, LABEL_MAX, "%s", iportname);
	      g_array_append_vals (devices, backend_device, 1);
	    }
#else
	  //We consider the cartesian product of inputs and outputs as the available ports.
	  backend_device = g_malloc (sizeof (struct backend_device));
	  backend_device->type = BE_TYPE_MIDI;
	  snprintf (backend_device->id, LABEL_MAX, "%s%s%s", iportname,
		    INPUT_OUTPUT_SEPARATOR, oportname);
	  snprintf (backend_device->name, LABEL_MAX, "%s%s%s", iportname,
		    INPUT_OUTPUT_SEPARATOR, oportname);
	  g_array_append_vals (devices, backend_device, 1);
#endif
	}
    }

cleanup_output:
  rtmidi_close_port (inputp);
  rtmidi_in_free (inputp);

cleanup_input:
  rtmidi_close_port (outputp);
  rtmidi_out_free (outputp);
}

const gchar *
backend_strerror (struct backend *backend, gint err)
{
  return backend->outputp->msg ? backend->outputp->msg : backend->inputp->msg;
}

const gchar *
backend_name ()
{
  return "RtMidi";
}
