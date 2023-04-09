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
#include "local.h"

// When sending a batch of SysEx messages we want the trasfer status to be controlled outside this function.
// This is what the update parameter is for.

gint backend_tx_sysex_internal (struct backend *, struct sysex_transfer *,
				gboolean);

void backend_rx_drain_int (struct backend *);
void backend_destroy_int (struct backend *);
gint backend_init_int (struct backend *, const gchar *);
gboolean backend_check_int (struct backend *);
const gchar *backend_name ();
const gchar *backend_version ();

//Identity Request Universal Sysex message
static const guint8 BE_MIDI_IDENTITY_REQUEST[] =
  { 0xf0, 0x7e, 0x7f, 6, 1, 0xf7 };

gdouble
backend_get_storage_stats_percent (struct backend_storage_stats *statfs)
{
  return (statfs->bsize - statfs->bfree) * 100.0 / statfs->bsize;
}

const struct fs_operations *
backend_get_fs_operations (struct backend *backend, gint fs,
			   const gchar * name)
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
      backend->cache = g_hash_table_new_full (g_bytes_hash, g_bytes_equal,
					      (GDestroyNotify) g_bytes_unref,
					      (GDestroyNotify) free_msg);
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
backend_midi_handshake (struct backend *backend)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint offset;

  backend->name[0] = 0;
  backend->version[0] = 0;
  backend->description[0] = 0;
  backend->fs_ops = NULL;
  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  memset (&backend->midi_info, 0, sizeof (struct backend_midi_info));

  tx_msg = g_byte_array_sized_new (sizeof (BE_MIDI_IDENTITY_REQUEST));
  g_byte_array_append (tx_msg, (guchar *) BE_MIDI_IDENTITY_REQUEST,
		       sizeof (BE_MIDI_IDENTITY_REQUEST));
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
				    BE_SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      debug_print (1, "No MIDI identity reply\n");
      return;
    }

  if (rx_msg->data[4] == 2)
    {
      if (rx_msg->len == 15 || rx_msg->len == 17)
	{
	  offset = rx_msg->len - 15;
	  memset (backend->midi_info.company, 0, BE_COMPANY_LEN);
	  memcpy (backend->midi_info.company, &rx_msg->data[5],
		  rx_msg->len == 15 ? 1 : BE_COMPANY_LEN);
	  memcpy (backend->midi_info.family, &rx_msg->data[6 + offset],
		  BE_FAMILY_LEN);
	  memcpy (backend->midi_info.model, &rx_msg->data[8 + offset],
		  BE_MODEL_LEN);
	  memcpy (backend->midi_info.version, &rx_msg->data[10 + offset],
		  BE_VERSION_LEN);

	  snprintf (backend->name, LABEL_MAX,
		    "%02x-%02x-%02x %02x-%02x %02x-%02x",
		    backend->midi_info.company[0],
		    backend->midi_info.company[1],
		    backend->midi_info.company[2],
		    backend->midi_info.family[0],
		    backend->midi_info.family[1],
		    backend->midi_info.model[0], backend->midi_info.model[1]);
	  snprintf (backend->version, LABEL_MAX, "%d.%d.%d.%d",
		    backend->midi_info.version[0],
		    backend->midi_info.version[1],
		    backend->midi_info.version[2],
		    backend->midi_info.version[3]);
	  debug_print (1, "Detected device: %s %s\n", backend->name,
		       backend->version);
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

gint
backend_tx_sysex_no_status (struct backend *backend,
			    struct sysex_transfer *transfer)
{
  return backend_tx_sysex_internal (backend, transfer, FALSE);
}

gint
backend_tx_sysex (struct backend *backend, struct sysex_transfer *transfer)
{
  return backend_tx_sysex_internal (backend, transfer, TRUE);
}

//Synchronized

gint
backend_tx (struct backend *backend, GByteArray * tx_msg)
{
  struct sysex_transfer transfer;
  transfer.raw = tx_msg;
  g_mutex_lock (&backend->mutex);
  backend_tx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  free_msg (tx_msg);
  return transfer.err;
}

//Not synchronized. Only meant to be called from backend_tx_and_rx_sysex_transfer.

static gint
backend_tx_and_rx_sysex_transfer_no_cache (struct backend *backend,
					   struct sysex_transfer *transfer,
					   gboolean free)
{
  transfer->batch = FALSE;

  backend_tx_sysex (backend, transfer);
  if (free)
    {
      free_msg (transfer->raw);
      transfer->raw = NULL;
    }
  if (!transfer->err)
    {
      backend_rx_sysex (backend, transfer);
    }

  return transfer->err;
}

//Synchronized

gint
backend_tx_and_rx_sysex_transfer (struct backend *backend,
				  struct sysex_transfer *transfer,
				  gboolean free)
{
  GBytes *key;
  GByteArray *rx_msg;
  transfer->batch = FALSE;

  g_mutex_lock (&backend->mutex);
  if (backend->cache)
    {
      key = g_bytes_new (transfer->raw->data, transfer->raw->len);
      rx_msg = g_hash_table_lookup (backend->cache, key);
      if (rx_msg)
	{
	  transfer->raw = g_byte_array_sized_new (rx_msg->len);
	  g_byte_array_append (transfer->raw, rx_msg->data, rx_msg->len);
	  transfer->err = 0;
	  g_bytes_unref (key);
	  goto end;
	}

      if (backend_tx_and_rx_sysex_transfer_no_cache (backend, transfer, free))
	{
	  g_bytes_unref (key);
	  goto end;
	}

      rx_msg = g_byte_array_sized_new (transfer->raw->len);
      g_byte_array_append (rx_msg, transfer->raw->data, transfer->raw->len);
      g_hash_table_insert (backend->cache, key, rx_msg);
    }
  else
    {
      backend_tx_and_rx_sysex_transfer_no_cache (backend, transfer, free);
    }

end:
  g_mutex_unlock (&backend->mutex);
  return transfer->err;
}

//Synchronized
//A timeout of 0 means infinity; a negative timeout means the default timeout.

GByteArray *
backend_tx_and_rx_sysex (struct backend *backend, GByteArray * tx_msg,
			 gint timeout)
{
  struct sysex_transfer transfer;
  transfer.raw = tx_msg;
  transfer.timeout = timeout < 0 ? BE_SYSEX_TIMEOUT_MS : timeout;
  backend_tx_and_rx_sysex_transfer (backend, &transfer, TRUE);
  return transfer.raw;
}

gchar *
backend_get_fs_ext (struct backend *backend, const struct fs_operations *ops)
{
  gchar *ext = malloc (LABEL_MAX);
  snprintf (ext, LABEL_MAX, "%s", ops->type_ext);
  return ext;
}

void
backend_destroy_data (struct backend *backend)
{
  debug_print (1, "Destroying backend data...\n");
  g_free (backend->data);
  backend->data = NULL;
}

gint
backend_program_change (struct backend *backend, guint8 channel,
			guint8 program)
{
  ssize_t size;
  guint8 msg[2];

  msg[0] = 0xc0 | (channel & 0xf);
  msg[1] = program & 0x7f;

  debug_print (1, "Sending MIDI program %d...\n", msg[1]);

  if ((size = backend_tx_raw (backend, msg, 2)) < 0)
    {
      return size;
    }
  return 0;
}

gint
backend_send_controller (struct backend *backend, guint8 channel,
			 guint8 controller, guint8 value)
{
  ssize_t size;
  guint8 msg[3];

  msg[0] = 0xb0 | (channel & 0xf);
  msg[1] = controller & 0x7f;
  msg[2] = value & 0x7f;

  debug_print (1, "Sending MIDI controller %d with value %d...\n", msg[1],
	       msg[2]);

  if ((size = backend_tx_raw (backend, msg, 3)) < 0)
    {
      return size;
    }
  return 0;
}

gint
backend_send_rpn (struct backend *backend, guint8 channel,
		  guint8 controller_msb, guint8 controller_lsb,
		  guint8 value_msb, guint8 value_lsb)
{
  gint err = backend_send_controller (backend, channel, 101, controller_msb);
  err |= backend_send_controller (backend, channel, 100, controller_lsb);
  err |= backend_send_controller (backend, channel, 6, value_msb);
  err |= backend_send_controller (backend, channel, 38, value_lsb);
  return err;
}

gint
backend_init (struct backend *backend, const gchar * id)
{
  debug_print (1, "Initializing backend (%s) to '%s'...\n",
	       backend_name (), id);
  backend->type = BE_TYPE_MIDI;
  gint err = backend_init_int (backend, id);
  if (!err)
    {
      g_mutex_lock (&backend->mutex);
      backend_rx_drain (backend);
      g_mutex_unlock (&backend->mutex);
    }
  return err;
}

void
backend_destroy (struct backend *backend)
{
  debug_print (1, "Destroying backend...\n");

  if (backend->destroy_data)
    {
      backend->destroy_data (backend);
    }

  backend_disable_cache (backend);

  if (backend->type == BE_TYPE_MIDI)
    {
      backend_destroy_int (backend);
    }

  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  backend->destroy_data = NULL;
  backend->type = BE_TYPE_NONE;
  backend->fs_ops = NULL;
}

gboolean
backend_check (struct backend *backend)
{
  switch (backend->type)
    {
    case BE_TYPE_MIDI:
      return backend_check_int (backend);
    case BE_TYPE_SYSTEM:
      return TRUE;
    default:
      return FALSE;
    }
}

static ssize_t
backend_rx_raw_loop (struct backend *backend, struct sysex_transfer *transfer)
{
  ssize_t rx_len, rx_len_msg;
  gchar *text;
  guint8 tmp[BE_TMP_BUFF_LEN];
  guint8 *tmp_msg, *data = backend->buffer + backend->rx_len;

  if (!backend->inputp)
    {
      error_print ("Input port is NULL\n");
      return -ENOTCONN;
    }

  debug_print (4, "Reading data...\n");

  while (1)
    {
      if (!transfer->active)
	{
	  return -ECANCELED;
	}

      debug_print (6, "Checking timeout (%d ms, %d ms, %s mode)...\n",
		   transfer->time, transfer->timeout,
		   transfer->batch ? "batch" : "single");
      if (((transfer->batch && transfer->status == RECEIVING)
	   || !transfer->batch) && transfer->timeout > -1
	  && transfer->time >= transfer->timeout)
	{
	  debug_print (1, "Timeout (%d)\n", transfer->timeout);
	  gchar *text = debug_get_hex_data (debug_level, backend->buffer,
					    backend->rx_len);
	  debug_print (4, "Internal buffer data (%zd): %s\n", backend->rx_len,
		       text);
	  free (text);
	  return -ETIMEDOUT;
	}

      rx_len = backend_rx_raw (backend, tmp, BE_TMP_BUFF_LEN);
      if (rx_len < 0)
	{
	  return rx_len;
	}
      if (rx_len == 0)
	{
	  if ((transfer->batch && transfer->status == RECEIVING)
	      || !transfer->batch)
	    {
	      transfer->time += BE_POLL_TIMEOUT_MS;
	    }
	  continue;
	}

      //Everything is skipped until a 0xf0 is found. This includes every RT MIDI message.
      tmp_msg = tmp;
      if (!backend->rx_len && *tmp_msg != 0xf0)
	{
	  if (debug_level >= 4)
	    {
	      gchar *text = debug_get_hex_data (debug_level, tmp, rx_len);
	      debug_print (4, "Skipping non SysEx data (%zd): %s\n", rx_len,
			   text);
	      free (text);
	    }

	  tmp_msg++;
	  rx_len_msg = 1;
	  for (gint i = 1; i < rx_len; i++, tmp_msg++, rx_len_msg++)
	    {
	      if (*tmp_msg == 0xf0)
		{
		  break;
		}
	    }
	  rx_len -= rx_len_msg;
	}

      if (rx_len == 0)
	{
	  transfer->time += BE_POLL_TIMEOUT_MS;
	  continue;
	}

      if (rx_len > 0)
	{
	  memcpy (backend->buffer + backend->rx_len, tmp_msg, rx_len);
	  backend->rx_len += rx_len;
	  break;
	}

      if (rx_len < 0)
	{
	  break;
	}
    }

  if (debug_level >= 3)
    {
      text = debug_get_hex_data (debug_level, data, rx_len);
      debug_print (3, "Queued data (%zu): %s\n", rx_len, text);
      free (text);
    }

  return rx_len;
}

//Access to this function must be synchronized.

gint
backend_rx_sysex (struct backend *backend, struct sysex_transfer *transfer)
{
  gint next_check, len, i;
  guint8 *b;
  ssize_t rx_len;

  transfer->err = 0;
  transfer->time = 0;
  transfer->active = TRUE;
  transfer->status = WAITING;
  transfer->raw = g_byte_array_sized_new (BE_INT_BUF_LEN);

  next_check = 0;
  while (1)
    {
      if (backend->rx_len == next_check)
	{
	  debug_print (4, "Reading from MIDI device...\n");
	  if (transfer->batch)
	    {
	      transfer->time = 0;
	    }
	  rx_len = backend_rx_raw_loop (backend, transfer);

	  if (rx_len == -ENODATA || rx_len == -ETIMEDOUT
	      || rx_len == -ECANCELED)
	    {
	      if (transfer->batch)
		{
		  break;
		}
	      else
		{
		  transfer->err = rx_len;
		  goto end;
		}
	    }
	  else if (rx_len < 0)
	    {
	      transfer->err = -EIO;
	      goto end;
	    }
	}
      else
	{
	  debug_print (4, "Reading from internal buffer...\n");
	}

      transfer->status = RECEIVING;
      len = -1;
      b = backend->buffer + next_check;
      for (; next_check < backend->rx_len; next_check++, b++)
	{
	  if (*b == 0xf7)
	    {
	      next_check++;
	      len = next_check;
	      break;
	    }
	}

      //We filter out whatever SysEx message not suitable for Elektroid.

      if (len > 0)
	{
	  //Filter out everything until an 0xf0 is found.
	  b = backend->buffer;
	  for (i = 0; i < len && *b != 0xf0; i++, b++);
	  if (i > 0 && debug_level >= 4)
	    {
	      gchar *text = debug_get_hex_data (debug_level, backend->buffer,
						i);
	      debug_print (4, "Skipping non SysEx data in buffer (%d): %s\n",
			   i, text);
	      free (text);
	    }
	  debug_print (3, "Copying %d bytes...\n", len - i);
	  g_byte_array_append (transfer->raw, b, len - i);

	  backend->rx_len -= len;
	  memmove (backend->buffer, backend->buffer + next_check,
		   backend->rx_len);
	  transfer->err = 0;
	  next_check = 0;

	  //Filter empty message
	  if (transfer->raw->len == 2
	      && !memcmp (transfer->raw->data, "\xf0\xf7", 2))
	    {
	      debug_print (4, "Removing empty message...\n");
	      g_byte_array_remove_range (transfer->raw, 0, 2);
	      continue;
	    }

	  if (debug_level >= 4)
	    {
	      gchar *text =
		debug_get_hex_data (debug_level, transfer->raw->data,
				    transfer->raw->len);
	      debug_print (4, "Queued data (%d): %s\n",
			   transfer->raw->len, text);
	      free (text);
	    }
	}
      else
	{
	  debug_print (4, "No message in the queue. Continuing...\n");
	}

      if (transfer->raw->len && !transfer->batch)
	{
	  break;
	}
    }

end:
  if (!transfer->raw->len)
    {
      transfer->err = -ETIMEDOUT;
    }
  if (transfer->err)
    {
      free_msg (transfer->raw);
      transfer->raw = NULL;
    }
  else
    {
      if (debug_level >= 2)
	{
	  gchar *text = debug_get_hex_data (debug_level, transfer->raw->data,
					    transfer->raw->len);
	  debug_print (2, "Raw message received (%d): %s\n",
		       transfer->raw->len, text);
	  free (text);
	}

    }
  transfer->active = FALSE;
  transfer->status = FINISHED;
  return transfer->err;
}

//Access to this function must be synchronized.

void
backend_rx_drain (struct backend *backend)
{
  struct sysex_transfer transfer;
  transfer.timeout = 1000;
  transfer.batch = FALSE;

  debug_print (2, "Draining buffers...\n");
  backend->rx_len = 0;
  backend_rx_drain_int (backend);
  while (!backend_rx_sysex (backend, &transfer))
    {
      free_msg (transfer.raw);
    }
}
