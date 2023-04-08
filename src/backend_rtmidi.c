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

#include "backend.h"
#include "rtmidi_c.h"

#if defined(__linux__)
#define ELEKTROID_RTMIDI_API RTMIDI_API_LINUX_ALSA
#define FIRST_OUTPUT_PORT 1	//Skip Midi Through
#else
#define WINDOWS_INPUT_OUTPUT_SEPARATOR " :: "
#define ELEKTROID_RTMIDI_API RTMIDI_API_WINDOWS_MM
#define FIRST_OUTPUT_PORT 1	//Skip Microsoft GS Wavetable Synth 0
#endif

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
backend_init_int (struct backend *backend, const gchar * id)
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
		       strlen (WINDOWS_INPUT_OUTPUT_SEPARATOR), oportname))
#endif
	    {
	      backend->inputp =
		rtmidi_in_create (ELEKTROID_RTMIDI_API, PACKAGE_NAME,
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
backend_tx_sysex_internal (struct backend *backend,
			   struct sysex_transfer *transfer, gboolean update)
{
  if (update)
    {
      transfer->err = 0;
      transfer->active = TRUE;
      transfer->status = SENDING;
    }

  rtmidi_out_send_message (backend->outputp, transfer->raw->data,
			   transfer->raw->len);
  transfer->err = backend->outputp->ok ? 0 : -EIO;

  if (!transfer->err && debug_level >= 2)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message sent (%d): %s\n", transfer->raw->len,
		   text);
      free (text);
    }

  if (update)
    {
      transfer->active = FALSE;
      transfer->status = FINISHED;
    }
  return transfer->err;
}

ssize_t
backend_tx_raw (struct backend *backend, const guint8 * data, guint len)
{
  struct sysex_transfer transfer;
  transfer.raw = g_byte_array_sized_new (len);
  g_byte_array_append (transfer.raw, data, len);
  backend_tx_sysex_internal (backend, &transfer, TRUE);
  return transfer.err ? transfer.err : len;
}

void
backend_rx_drain (struct backend *backend)
{
  size_t len;

  debug_print (2, "Draining buffers...\n");
  backend->rx_len = 0;
  while (1)
    {
      len = BE_INT_BUF_LEN;
      rtmidi_in_get_message (backend->inputp, backend->buffer, &len);
      if (len == 0)
	{
	  break;
	}
    }
}

//Access to this function must be synchronized.

gint
backend_rx_sysex (struct backend *backend, struct sysex_transfer *transfer)
{
  size_t len;

  if (!backend->inputp)
    {
      error_print ("Input port is NULL\n");
      return -ENOTCONN;
    }

  transfer->time = 0;
  transfer->err = -1;
  transfer->active = TRUE;
  transfer->status = WAITING;
  transfer->raw = g_byte_array_sized_new (BE_INT_BUF_LEN);

  while (1)
    {
      if (!transfer->active)
	{
	  transfer->err = -ECANCELED;
	  goto end;
	}

      len = BE_INT_BUF_LEN;
      rtmidi_in_get_message (backend->inputp, backend->buffer, &len);
      if (!backend->inputp->ok)
	{
	  transfer->err = -EIO;
	  goto end;
	}

      if (len && backend->buffer[0] == 0xf0)	//We filter out everything is not a SysEx message.
	{
	  g_byte_array_append (transfer->raw, backend->buffer, len);
	  transfer->time = 0;
	  transfer->status = RECEIVING;
	  transfer->err = 0;
	  if (!transfer->batch)
	    {
	      break;
	    }
	}
      else
	{
	  usleep (BE_POLL_TIMEOUT_MS * 1000);
	  transfer->time += BE_POLL_TIMEOUT_MS;
	}

      debug_print (4, "Checking timeout (%d ms, %d ms, %s mode)...\n",
		   transfer->time, transfer->timeout,
		   transfer->batch ? "batch" : "single");
      if (((transfer->batch && transfer->status == RECEIVING)
	   || !transfer->batch) && transfer->timeout > -1
	  && transfer->time >= transfer->timeout)
	{
	  debug_print (1, "Timeout\n");
	  transfer->err = -ETIMEDOUT;
	  goto end;
	}
    }

  if (transfer->raw->len && debug_level >= 2)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message received (%d): %s\n", transfer->raw->len,
		   text);
      free (text);
    }

end:
  if (transfer->err < 0)
    {
      free_msg (transfer->raw);
      transfer->raw = NULL;
    }
  transfer->active = FALSE;
  transfer->status = FINISHED;
  return transfer->err;
}

gboolean
backend_check_int (struct backend *backend)
{
  return backend->inputp && backend->outputp;
}

GArray *
backend_get_system_devices ()
{
  struct RtMidiWrapper *inputp;
  struct RtMidiWrapper *outputp;
  guint iports, oports;
  gchar iportname[LABEL_MAX];
  gchar oportname[LABEL_MAX];
  gint iportnamelen, oportnamelen;
  struct backend_system_device *backend_system_device;
  GArray *devices = g_array_new (FALSE, FALSE,
				 sizeof (struct backend_system_device));

  if (!(inputp = rtmidi_in_create_default ()))
    {
      goto end;
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
	  debug_print (3, "Checking I/O availability (%s == %s)...\n",
		       iportname, oportname);
#if defined(__linux__)
	  if (!strcmp (iportname, oportname))
	    {
	      backend_system_device =
		malloc (sizeof (struct backend_system_device));
	      snprintf (backend_system_device->id, LABEL_MAX, "%s",
			iportname);
	      snprintf (backend_system_device->name, LABEL_MAX, "%s",
			iportname);
	      g_array_append_vals (devices, backend_system_device, 1);
	    }
#else
	  //We consider the cartesian product of inputs and outputs as the available ports.
	  backend_system_device =
	    malloc (sizeof (struct backend_system_device));
	  snprintf (backend_system_device->id, LABEL_MAX, "%s%s%s",
		    iportname, WINDOWS_INPUT_OUTPUT_SEPARATOR, oportname);
	  snprintf (backend_system_device->name, LABEL_MAX, "%s%s%s",
		    iportname, WINDOWS_INPUT_OUTPUT_SEPARATOR, oportname);
	  g_array_append_vals (devices, backend_system_device, 1);
#endif
	}
    }

cleanup_output:
  rtmidi_close_port (inputp);
  rtmidi_in_free (inputp);

cleanup_input:
  rtmidi_close_port (outputp);
  rtmidi_out_free (outputp);

end:
  return devices;
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
