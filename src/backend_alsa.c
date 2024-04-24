/*
 *   backend_alsa.c
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

void
backend_destroy_int (struct backend *backend)
{
  gint err;

  if (backend->inputp)
    {
      err = snd_rawmidi_close (backend->inputp);
      if (err)
	{
	  error_print ("Error while closing MIDI port: %s\n",
		       snd_strerror (err));
	}
      backend->inputp = NULL;
    }

  if (backend->outputp)
    {
      err = snd_rawmidi_close (backend->outputp);
      if (err)
	{
	  error_print ("Error while closing MIDI port: %s\n",
		       snd_strerror (err));
	}
      backend->outputp = NULL;
    }

  if (backend->buffer)
    {
      g_free (backend->buffer);
      backend->buffer = NULL;
    }

  if (backend->pfds)
    {
      g_free (backend->pfds);
      backend->pfds = NULL;
    }
}

gint
backend_init_int (struct backend *backend, const gchar *id)
{
  snd_rawmidi_params_t *params;
  gint err;

  backend->inputp = NULL;
  backend->outputp = NULL;
  backend->pfds = NULL;
  backend->rx_len = 0;
  backend->buffer = NULL;

  backend->buffer = g_malloc (sizeof (guint8) * BE_INT_BUF_LEN);

  if ((err = snd_rawmidi_open (&backend->inputp, &backend->outputp, id,
			       SND_RAWMIDI_NONBLOCK | SND_RAWMIDI_SYNC)) < 0)
    {
      error_print ("Error while opening MIDI port: %s\n", g_strerror (-err));
      goto cleanup;
    }

  debug_print (1, "Setting blocking mode...\n");
  if ((err = snd_rawmidi_nonblock (backend->outputp, 0)) < 0)
    {
      error_print ("Error while setting blocking mode\n");
      goto cleanup;
    }
  if ((err = snd_rawmidi_nonblock (backend->inputp, 1)) < 0)
    {
      error_print ("Error while setting blocking mode\n");
      goto cleanup;
    }

  backend->npfds = snd_rawmidi_poll_descriptors_count (backend->inputp);
  backend->pfds = g_malloc (backend->npfds * sizeof (struct pollfd));

  snd_rawmidi_poll_descriptors (backend->inputp, backend->pfds,
				backend->npfds);
  err = snd_rawmidi_params_malloc (&params);
  if (err)
    {
      goto cleanup;
    }

  err = snd_rawmidi_params_current (backend->inputp, params);
  if (err)
    {
      goto cleanup_params;
    }

  err =
    snd_rawmidi_params_set_buffer_size (backend->inputp, params,
					BE_DEV_RING_BUF_LEN);
  if (err)
    {
      goto cleanup_params;
    }

  err = snd_rawmidi_params (backend->inputp, params);
  if (err)
    {
      goto cleanup_params;
    }

  return 0;

cleanup_params:
  snd_rawmidi_params_free (params);
cleanup:
  backend_destroy (backend);
  g_free (backend->buffer);
  return err;
}

ssize_t
backend_tx_raw (struct backend *backend, guint8 *data, guint len)
{
  ssize_t tx_len;

  if (!backend->outputp)
    {
      error_print ("Output port is NULL\n");
      return -ENOTCONN;
    }

  snd_rawmidi_read (backend->inputp, NULL, 0);	// trigger reading

  tx_len = snd_rawmidi_write (backend->outputp, data, len);
  if (tx_len < 0)
    {
      error_print ("Error while writing to device: %s\n",
		   snd_strerror (tx_len));
    }
  return tx_len;
}

gint
backend_tx_sysex_internal (struct backend *backend,
			   struct sysex_transfer *transfer, gboolean update)
{
  ssize_t tx_len;
  guint total;
  guint len;
  guchar *b;

  if (update)
    {
      transfer->err = 0;
      transfer->active = TRUE;
      transfer->status = SENDING;
    }

  b = transfer->raw->data;
  total = 0;
  while (total < transfer->raw->len && transfer->active)
    {
      len = transfer->raw->len - total;
      if (len > BE_MAX_TX_LEN)
	{
	  len = BE_MAX_TX_LEN;
	}

      tx_len = backend_tx_raw (backend, b, len);
      if (tx_len < 0)
	{
	  transfer->err = tx_len;
	  break;
	}
      b += len;
      total += len;
    }

  if (!transfer->active)
    {
      transfer->err = -ECANCELED;
    }

  if (!transfer->err && debug_level >= 2)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message sent (%d): %s\n", transfer->raw->len,
		   text);
      g_free (text);
    }

  if (update)
    {
      transfer->active = FALSE;
      transfer->status = FINISHED;
    }
  return transfer->err;
}

void
backend_rx_drain_int (struct backend *backend)
{
  snd_rawmidi_drain (backend->inputp);
}

ssize_t
backend_rx_raw (struct backend *backend, guint8 *buffer, guint len)
{
  gint err;
  ssize_t rx_len;
  unsigned short revents;

  debug_print (6, "Polling...\n");
  err = poll (backend->pfds, backend->npfds, BE_POLL_TIMEOUT_MS);
  if (err == 0)
    {
      return 0;
    }

  if (err < 0)
    {
      error_print ("Error while polling. %s.\n", g_strerror (errno));
      if (errno == EINTR)
	{
	  return -ECANCELED;
	}
      return err;
    }

  if ((err = snd_rawmidi_poll_descriptors_revents (backend->inputp,
						   backend->pfds,
						   backend->npfds,
						   &revents)) < 0)
    {
      error_print ("Error while getting poll events. %s.\n",
		   snd_strerror (err));
      return err;
    }

  if (revents & (POLLERR | POLLHUP))
    {
      return -ENODATA;
    }

  if (!(revents & POLLIN))
    {
      return 0;
    }

  rx_len = snd_rawmidi_read (backend->inputp, buffer, len);
  if (rx_len == -EAGAIN)
    {
      return 0;
    }

  if (rx_len < 0)
    {
      error_print ("Error while reading from device: %s\n",
		   snd_strerror (rx_len));
    }

  return rx_len;
}

gboolean
backend_check_int (struct backend *backend)
{
  return backend->inputp && backend->outputp;
}

static void
backend_get_system_subdevices (snd_ctl_t *ctl, int card, int device,
			       GArray *devices)
{
  snd_rawmidi_info_t *info;
  const gchar *name;
  const gchar *sub_name;
  int subs, subs_in, subs_out;
  int sub;
  int err;
  struct backend_device *backend_device;

  snd_rawmidi_info_alloca (&info);
  snd_rawmidi_info_set_device (info, device);
  snd_rawmidi_info_set_stream (info, SND_RAWMIDI_STREAM_INPUT);
  err = snd_ctl_rawmidi_info (ctl, info);
  if (err >= 0)
    {
      subs_in = snd_rawmidi_info_get_subdevices_count (info);
    }
  else
    {
      subs_in = 0;
    }

  snd_rawmidi_info_set_stream (info, SND_RAWMIDI_STREAM_OUTPUT);
  err = snd_ctl_rawmidi_info (ctl, info);
  if (err >= 0)
    {
      subs_out = snd_rawmidi_info_get_subdevices_count (info);
    }
  else
    {
      subs_out = 0;
    }

  subs = subs_in > subs_out ? subs_in : subs_out;
  if (!subs)
    {
      return;
    }

  if (subs_in <= 0 || subs_out <= 0)
    {
      return;
    }

  for (sub = 0; sub < subs; sub++)
    {
      snd_rawmidi_info_set_subdevice (info, sub);

      snd_rawmidi_info_set_stream (info, SND_RAWMIDI_STREAM_INPUT);
      err = snd_ctl_rawmidi_info (ctl, info);
      if (err < 0)
	{
	  debug_print (1,
		       "Cannot get rawmidi input information %d:%d:%d: %s\n",
		       card, device, sub, snd_strerror (err));
	  continue;
	}

      snd_rawmidi_info_set_stream (info, SND_RAWMIDI_STREAM_OUTPUT);
      err = snd_ctl_rawmidi_info (ctl, info);
      if (err < 0)
	{
	  debug_print (1,
		       "Cannot get rawmidi output information %d:%d:%d: %s\n",
		       card, device, sub, snd_strerror (err));
	  continue;
	}

      name = snd_rawmidi_info_get_name (info);
      sub_name = snd_rawmidi_info_get_subdevice_name (info);

      debug_print (1, "Adding hw:%d (name '%s', subname '%s')...\n", card,
		   name, sub_name);
      backend_device = g_malloc (sizeof (struct backend_device));
      backend_device->type = BE_TYPE_MIDI;
      snprintf (backend_device->id, LABEL_MAX, BE_DEVICE_NAME, card, device,
		sub);
      snprintf (backend_device->name, LABEL_MAX, BE_DEVICE_NAME ": %s%s%s",
		card, device, sub, name, strlen (sub_name) ? ", " : "",
		sub_name);

      g_array_append_vals (devices, backend_device, 1);
    }
}

static void
backend_fill_card_devices (gint card, GArray *devices)
{
  snd_ctl_t *ctl;
  gchar name[32];
  gint device;
  gint err;

  sprintf (name, "hw:%d", card);
  if ((err = snd_ctl_open (&ctl, name, 0)) < 0)
    {
      error_print ("Cannot open control for card %d: %s\n",
		   card, snd_strerror (err));
      return;
    }
  device = -1;
  while (!(err = snd_ctl_rawmidi_next_device (ctl, &device)) && device >= 0)
    {
      backend_get_system_subdevices (ctl, card, device, devices);
    }
  if (device >= 0 && err < 0)
    {
      error_print ("Cannot determine device number %d: %s\n", device,
		   snd_strerror (err));
    }
  snd_ctl_close (ctl);
}

void
backend_fill_devices_array (GArray *devices)
{
  gint card, err;
  card = -1;
  while (!(err = snd_card_next (&card)) && card >= 0)
    {
      backend_fill_card_devices (card, devices);
    }
  if (card >= 0 && err < 0)
    {
      error_print ("Cannot determine card number %d: %s\n", card,
                   snd_strerror (err));
    }
}

const gchar *
backend_strerror (struct backend *backend, gint err)
{
  return snd_strerror (err);
}

const gchar *
backend_name ()
{
  return "ALSA";
}
