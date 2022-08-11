/*
 *   backend.c
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

#define POLL_TIMEOUT_MS 20
#define KB 1024
#define BUFF_SIZE (4 * KB)
#define RING_BUFF_SIZE (256 * KB)

//Identity Request Universal Sysex message
static const guint8 MIDI_IDENTITY_REQUEST[] =
  { 0xf0, 0x7e, 0x7f, 6, 1, 0xf7 };

gdouble
backend_get_storage_stats_percent (struct backend_storage_stats *statfs)
{
  return (statfs->bsize - statfs->bfree) * 100.0 / statfs->bsize;
}

const struct fs_operations *
backend_get_fs_operations (struct backend *backend, gint fs, const char *name)
{
  const struct fs_operations **fs_operations = backend->fs_ops;
  if (!fs_operations)
    {
      return NULL;
    }
  while (*fs_operations)
    {
      const struct fs_operations *ops = *fs_operations;
      if (ops->fs == fs || (name && !strcmp (ops->name, name)))
	{
	  return ops;
	}
      fs_operations++;
    }
  return NULL;
}

void
backend_enable_cache (struct backend *backend)
{
  g_mutex_lock (&backend->mutex);
  if (!backend->cache)
    {
      backend->cache =
	g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_msg);
    }
  g_mutex_unlock (&backend->mutex);
}

void
backend_disable_cache (struct backend *backend)
{
  g_mutex_lock (&backend->mutex);
  if (backend->cache)
    {
      g_hash_table_destroy (backend->cache);
      backend->cache = NULL;
    }
  g_mutex_unlock (&backend->mutex);
}

void
backend_destroy (struct backend *backend)
{
  gint err;

  debug_print (1, "Destroying backend...\n");

  backend->device_desc.id = -1;
  backend->device_desc.filesystems = 0;

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
      free (backend->buffer);
      backend->buffer = NULL;
    }

  if (backend->pfds)
    {
      free (backend->pfds);
      backend->pfds = NULL;
    }

  if (backend->destroy_data)
    {
      backend->destroy_data (backend);
    }

  backend_disable_cache (backend);
}

static void
backend_midi_handshake (struct backend *backend)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint offset;

  backend->device_desc.id = -1;
  backend->device_desc.storage = 0;
  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  backend->device_desc.name[0] = 0;
  backend->device_desc.alias[0] = 0;
  backend->device_name[0] = 0;
  backend->fs_ops = NULL;
  backend->destroy_data = NULL;

  tx_msg = g_byte_array_sized_new (sizeof (MIDI_IDENTITY_REQUEST));
  g_byte_array_append (tx_msg, (guchar *) MIDI_IDENTITY_REQUEST,
		       sizeof (MIDI_IDENTITY_REQUEST));
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
  if (rx_msg)
    {
      if (rx_msg->data[4] == 2)
	{
	  if (rx_msg->len == 15 || rx_msg->len == 17)
	    {
	      offset = rx_msg->len - 15;
	      memcpy (backend->midi_info.company, &rx_msg->data[5],
		      BE_COMPANY_LEN);
	      memcpy (backend->midi_info.family, &rx_msg->data[6 + offset],
		      BE_FAMILY_LEN);
	      memcpy (backend->midi_info.model, &rx_msg->data[8 + offset],
		      BE_MODEL_LEN);
	      memcpy (backend->midi_info.version, &rx_msg->data[10 + offset],
		      BE_VERSION_LEN);

	      snprintf (backend->device_name, LABEL_MAX,
			"%02x-%02x-%02x %02x-%02x %02x-%02x %d.%d.%d.%d",
			backend->midi_info.company[0],
			offset ? backend->midi_info.company[1] : 0,
			offset ? backend->midi_info.company[2] : 0,
			backend->midi_info.family[0],
			backend->midi_info.family[1],
			backend->midi_info.model[0],
			backend->midi_info.model[1],
			backend->midi_info.version[0],
			backend->midi_info.version[1],
			backend->midi_info.version[2],
			backend->midi_info.version[3]);
	      snprintf (backend->device_desc.name, LABEL_MAX,
			backend->device_name);
	      debug_print (1, "Detected device: %s\n", backend->device_name);
	    }
	  else
	    {
	      debug_print (1, "Illegal MIDI identity reply length\n");
	    }
	}
      else
	{
	  debug_print (1, "Illegal SUB-ID2\n");
	}

      free_msg (rx_msg);
    }
  else
    {
      debug_print (1, "No MIDI identity reply\n");
    }
}

gint
backend_init (struct backend *backend, gint card)
{
  snd_rawmidi_params_t *params;
  gint err;
  gchar name[32];

  backend->inputp = NULL;
  backend->outputp = NULL;
  backend->pfds = NULL;
  backend->rx_len = 0;
  backend->cache = NULL;
  backend->buffer = g_malloc (sizeof (guint8) * BUFF_SIZE);

  if (card < 0)
    {
      debug_print (1, "Invalid card\n");
      err = -EINVAL;
      goto cleanup;
    }

  sprintf (name, "hw:%d", card);

  debug_print (1, "Initializing backend to '%s'...\n", name);

  if ((err =
       snd_rawmidi_open (&backend->inputp, &backend->outputp,
			 name, SND_RAWMIDI_NONBLOCK | SND_RAWMIDI_SYNC)) < 0)
    {
      error_print ("Error while opening MIDI port: %s\n", g_strerror (err));
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

  debug_print (1, "Stopping device...\n");
  if (snd_rawmidi_write (backend->outputp, "\xfc", 1) < 0)
    {
      error_print ("Error while stopping device\n");
    }

  backend->npfds = snd_rawmidi_poll_descriptors_count (backend->inputp);
  backend->pfds = malloc (backend->npfds * sizeof (struct pollfd));
  if (!backend->buffer)
    {
      goto cleanup;
    }
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
					RING_BUFF_SIZE);
  if (err)
    {
      goto cleanup_params;
    }

  err = snd_rawmidi_params (backend->inputp, params);
  if (err)
    {
      goto cleanup_params;
    }

  backend_midi_handshake (backend);

  return 0;

cleanup_params:
  snd_rawmidi_params_free (params);
cleanup:
  backend_destroy (backend);
  g_free (backend->buffer);
  return -1;
}

static ssize_t
backend_tx_raw (struct backend *backend, const guint8 * data, guint len)
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
backend_tx_sysex (struct backend *backend, struct sysex_transfer *transfer)
{
  ssize_t tx_len;
  guint total;
  guint len;
  guchar *b;
  gint res = 0;

  transfer->status = SENDING;

  b = transfer->raw->data;
  total = 0;
  while (total < transfer->raw->len && transfer->active)
    {
      len = transfer->raw->len - total;
      if (len > BUFF_SIZE)
	{
	  len = BUFF_SIZE;
	}

      tx_len = backend_tx_raw (backend, b, len);
      if (tx_len < 0)
	{
	  res = tx_len;
	  break;
	}
      b += len;
      total += len;
    }

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message sent (%d): %s\n", transfer->raw->len,
		   text);
      free (text);
    }

  transfer->active = FALSE;
  transfer->status = FINISHED;
  return res;
}

void
backend_rx_drain (struct backend *backend)
{
  debug_print (2, "Draining buffer...\n");
  backend->rx_len = 0;
  snd_rawmidi_drain (backend->inputp);
}

static gboolean
backend_is_rt_msg (guint8 * data, guint len)
{
  guint i;
  guint8 *b;

  for (i = 0, b = data; i < len; i++, b++)
    {
      if (*b < 0xf8)		//Not System Real-Time Messages
	{
	  return FALSE;
	}
    }

  return TRUE;
}

static ssize_t
backend_rx_raw (struct backend *backend, guint8 * data, guint len,
		struct sysex_transfer *transfer)
{
  ssize_t rx_len;
  guint total_time;
  unsigned short revents;
  gint err;
  gchar *text;

  if (!backend->inputp)
    {
      error_print ("Input port is NULL\n");
      return -ENOTCONN;
    }

  total_time = 0;

  while (1)
    {
      err = poll (backend->pfds, backend->npfds, POLL_TIMEOUT_MS);

      if (!transfer->active)
	{
	  return -ECANCELED;
	}

      if (err == 0)
	{
	  total_time += POLL_TIMEOUT_MS;
	  if (((transfer->batch && transfer->status == RECEIVING)
	       || !transfer->batch) && transfer->timeout > -1
	      && total_time >= transfer->timeout)
	    {
	      debug_print (1, "Timeout!\n");
	      return -ETIMEDOUT;
	    }
	  continue;
	}

      if (err < 0)
	{
	  error_print ("Error while polling. %s.\n", g_strerror (errno));
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
	  continue;
	}

      rx_len = snd_rawmidi_read (backend->inputp, data, len);

      if (rx_len == -EAGAIN || rx_len == 0)
	{
	  continue;
	}

      if (rx_len > 0)
	{
	  if (backend_is_rt_msg (data, rx_len))
	    {
	      total_time += POLL_TIMEOUT_MS;
	      if (transfer->timeout > -1 && total_time >= transfer->timeout)
		{
		  debug_print (1, "Timeout!\n");
		  return -ETIMEDOUT;
		}
	      continue;
	    }
	  break;
	}

      if (rx_len < 0)
	{
	  error_print ("Error while reading from device: %s\n",
		       snd_strerror (rx_len));
	  break;
	}

    }

  if (debug_level > 2)
    {
      text = debug_get_hex_data (debug_level, data, rx_len);
      debug_print (3, "Buffer content (%zu): %s\n", rx_len, text);
      free (text);
    }

  return rx_len;
}

gint
backend_rx_sysex (struct backend *backend, struct sysex_transfer *transfer)
{
  gint i;
  guint8 *b;
  gint res = 0;

  transfer->status = WAITING;
  transfer->raw = g_byte_array_sized_new (BUFF_SIZE);

  i = 0;
  if (backend->rx_len < 0)
    {
      backend->rx_len = 0;
    }
  b = backend->buffer;

  while (1)
    {
      if (i == backend->rx_len)
	{
	  backend->rx_len = backend_rx_raw (backend, backend->buffer,
					    BUFF_SIZE, transfer);

	  if (backend->rx_len == -ENODATA || backend->rx_len == -ETIMEDOUT ||
	      backend->rx_len == -ECANCELED)
	    {
	      res = -backend->rx_len;
	      goto error;
	    }

	  if (backend->rx_len < 0)
	    {
	      res = -EIO;
	      goto error;
	    }

	  b = backend->buffer;
	  i = 0;
	}

      while (i < backend->rx_len && *b != 0xf0)
	{
	  b++;
	  i++;
	}

      if (i < backend->rx_len)
	{
	  break;
	}
    }

  g_byte_array_append (transfer->raw, b, 1);
  b++;
  i++;
  transfer->status = RECEIVING;

  while (1)
    {
      if (i == backend->rx_len)
	{
	  backend->rx_len = backend_rx_raw (backend, backend->buffer,
					    BUFF_SIZE, transfer);

	  if (backend->rx_len == -ENODATA && transfer->batch)
	    {
	      break;
	    }

	  if (backend->rx_len < 0)
	    {
	      res = -EIO;
	      goto error;
	    }

	  b = backend->buffer;
	  i = 0;
	}

      while (i < backend->rx_len && (*b != 0xf7 || transfer->batch))
	{
	  if (!backend_is_rt_msg (b, 1))
	    {
	      g_byte_array_append (transfer->raw, b, 1);
	    }
	  b++;
	  i++;
	}

      if (i < backend->rx_len)
	{
	  g_byte_array_append (transfer->raw, b, 1);
	  backend->rx_len = backend->rx_len - i - 1;
	  if (backend->rx_len > 0)
	    {
	      memmove (backend->buffer, &backend->buffer[i + 1],
		       backend->rx_len);
	    }
	  break;
	}
    }

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					transfer->raw->len);
      debug_print (2, "Raw message received (%d): %s\n", transfer->raw->len,
		   text);
      free (text);
    }

  goto end;

error:
  free_msg (transfer->raw);
  transfer->raw = NULL;
end:
  transfer->active = FALSE;
  transfer->status = FINISHED;
  return res;
}

//Synchronized

GByteArray *
backend_tx_and_rx_sysex_with_options (struct backend *backend,
				      GByteArray * tx_msg, gint timeout,
				      gboolean drain, gboolean free)
{
  gint err;
  struct sysex_transfer transfer;

  g_mutex_lock (&backend->mutex);

  if (drain)
    {
      backend_rx_drain (backend);
    }

  transfer.raw = tx_msg;
  transfer.active = TRUE;
  transfer.timeout = timeout < 0 ? SYSEX_TIMEOUT_MS : timeout;
  transfer.batch = FALSE;
  err = backend_tx_sysex (backend, &transfer);

  if (free)
    {
      free_msg (transfer.raw);
    }

  if (err < 0)
    {
      err = -EIO;
      goto cleanup;
    }

  transfer.active = TRUE;
  err = backend_rx_sysex (backend, &transfer);
  if (err < 0)
    {
      err = -EIO;
      goto cleanup;
    }

cleanup:
  g_mutex_unlock (&backend->mutex);
  return transfer.raw;
}

GByteArray *
backend_tx_and_rx_sysex (struct backend *backend, GByteArray * tx_msg,
			 gint timeout)
{
  return backend_tx_and_rx_sysex_with_options (backend, tx_msg, timeout, TRUE,
					       TRUE);
}

gboolean
backend_check (struct backend *backend)
{
  return (backend->inputp && backend->outputp);
}

static struct backend_system_device *
backend_get_system_device (snd_ctl_t * ctl, int card, int device)
{
  snd_rawmidi_info_t *info;
  const gchar *name;
  const gchar *sub_name;
  int subs, subs_in, subs_out;
  int sub;
  int err;
  struct backend_system_device *backend_system_device;

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
      return NULL;
    }

  if (subs_in <= 0 || subs_out <= 0)
    {
      return NULL;
    }

  sub = 0;
  snd_rawmidi_info_set_stream (info, sub < subs_in ?
			       SND_RAWMIDI_STREAM_INPUT :
			       SND_RAWMIDI_STREAM_OUTPUT);
  snd_rawmidi_info_set_subdevice (info, sub);
  err = snd_ctl_rawmidi_info (ctl, info);
  if (err < 0)
    {
      error_print ("Cannot get rawmidi information %d:%d:%d: %s\n",
		   card, device, sub, snd_strerror (err));
      return NULL;
    }

  name = snd_rawmidi_info_get_name (info);
  sub_name = snd_rawmidi_info_get_subdevice_name (info);

  debug_print (1, "Adding hw:%d (%s) %s...\n", card, name, sub_name);
  backend_system_device = malloc (sizeof (struct backend_system_device));
  backend_system_device->card = card;
  backend_system_device->name = strdup (sub_name);
  return backend_system_device;
}

static void
backend_fill_card_elektron_devices (gint card, GArray * devices)
{
  snd_ctl_t *ctl;
  gchar name[32];
  gint device;
  gint err;
  struct backend_system_device *backend_system_device;

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
      backend_system_device = backend_get_system_device (ctl, card, device);
      if (backend_system_device)
	{
	  g_array_append_vals (devices, backend_system_device, 1);
	}
    }
  if (err < 0)
    {
      error_print ("Cannot determine device number: %s\n",
		   snd_strerror (err));
    }
  snd_ctl_close (ctl);
}

GArray *
backend_get_system_devices ()
{
  gint card, err;
  GArray *devices =
    g_array_new (FALSE, FALSE, sizeof (struct backend_system_device));

  card = -1;
  while (!(err = snd_card_next (&card)) && card >= 0)
    {
      backend_fill_card_elektron_devices (card, devices);
    }
  if (err < 0)
    {
      error_print ("Cannot determine card number: %s\n", snd_strerror (err));
    }

  return devices;
}

gchar *
backend_get_fs_ext (const struct device_desc *desc,
		    const struct fs_operations *ops)
{
  gchar *ext = malloc (LABEL_MAX);
  snprintf (ext, LABEL_MAX, "%s", ops->type_ext);
  return ext;
}
