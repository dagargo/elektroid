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

#include <stdarg.h>
#include "backend.h"
#include "local.h"
#include "sample.h"
#include "preferences.h"

struct connector *system_connector = NULL;
GSList *connectors = NULL;

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
void backend_fill_devices_array (GArray *);

//Identity Request Universal Sysex message
static const guint8 BE_MIDI_IDENTITY_REQUEST[] =
  { 0xf0, 0x7e, 0x7f, 6, 1, 0xf7 };

gdouble
backend_get_storage_stats_percent (struct backend_storage_stats *statfs)
{
  return (statfs->bsize - statfs->bfree) * 100.0 / statfs->bsize;
}

static gint
backend_get_fs_operations_id_comparator (gconstpointer a, gconstpointer b)
{
  const struct fs_operations *ops = a;
  return ops->id != *((guint32 *) b);
}

const struct fs_operations *
backend_get_fs_operations_by_id (struct backend *backend, guint32 id)
{
  GSList *e = g_slist_find_custom (backend->fs_ops, &id,
				   backend_get_fs_operations_id_comparator);
  return e ? e->data : NULL;
}

static gint
backend_get_fs_operations_name_comparator (gconstpointer a, gconstpointer b)
{
  const struct fs_operations *ops = a;
  return strcmp (ops->name, (gchar *) b);
}

const struct fs_operations *
backend_get_fs_operations_by_name (struct backend *backend, const gchar *name)
{
  GSList *e = g_slist_find_custom (backend->fs_ops, name,
				   backend_get_fs_operations_name_comparator);
  return e ? e->data : NULL;
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
      debug_print (1, "No MIDI identity reply");
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
	  debug_print (1, "Detected device: %s %s", backend->name,
		       backend->version);
	}
      else
	{
	  debug_print (1, "Illegal MIDI identity reply length");
	}
    }
  else
    {
      debug_print (1, "Illegal SUB-ID2");
    }

  free_msg (rx_msg);

  usleep (BE_REST_TIME_US);
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
backend_tx (struct backend *backend, GByteArray *tx_msg)
{
  struct sysex_transfer transfer;
  transfer.raw = tx_msg;
  g_mutex_lock (&backend->mutex);
  backend_tx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  free_msg (tx_msg);
  return transfer.err;
}

//Synchronized

gint
backend_tx_and_rx_sysex_transfer (struct backend *backend,
				  struct sysex_transfer *transfer,
				  gboolean free)
{
  transfer->batch = FALSE;

  g_mutex_lock (&backend->mutex);

  if (transfer->raw)
    {
      backend_tx_sysex (backend, transfer);
      if (free)
	{
	  free_msg (transfer->raw);
	  transfer->raw = NULL;
	}
    }
  if (!transfer->err)
    {
      backend_rx_sysex (backend, transfer);
    }

  g_mutex_unlock (&backend->mutex);

  return transfer->err;
}

//Synchronized
//A timeout of 0 means infinity; a negative timeout means the default timeout.

GByteArray *
backend_tx_and_rx_sysex (struct backend *backend, GByteArray *tx_msg,
			 gint timeout)
{
  struct sysex_transfer transfer;
  transfer.raw = tx_msg;
  transfer.timeout = timeout < 0 ? BE_SYSEX_TIMEOUT_MS : timeout;
  backend_tx_and_rx_sysex_transfer (backend, &transfer, TRUE);
  return transfer.raw;
}

void
backend_destroy_data (struct backend *backend)
{
  debug_print (1, "Destroying backend data...");
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

  debug_print (1, "Sending MIDI program %d...", msg[1]);

  if ((size = backend_tx_raw (backend, msg, 2)) < 0)
    {
      return size;
    }
  return 0;
}

gint
backend_send_3_byte_message (struct backend *backend, guint8 msg_type,
			     guint8 channel, guint8 d1, guint8 d2)
{
  ssize_t size;
  guint8 msg[3];

  msg[0] = msg_type | (channel & 0xf);
  msg[1] = d1 & 0x7f;
  msg[2] = d2 & 0x7f;

  debug_print (1, "Sending MIDI message: status %08x; data %d, %d...",
	       msg[0], msg[1], msg[2]);

  if ((size = backend_tx_raw (backend, msg, 3)) < 0)
    {
      return size;
    }
  return 0;
}

gint
backend_send_controller (struct backend *backend, guint8 channel,
			 guint8 controller, guint8 value)
{
  return backend_send_3_byte_message (backend, 0xb0, channel, controller,
				      value);
}

gint
backend_send_note_on (struct backend *backend, guint8 channel,
		      guint8 note, guint8 velocity)
{
  return backend_send_3_byte_message (backend, 0x90, channel, note, velocity);
}

gint
backend_send_note_off (struct backend *backend, guint8 channel,
		       guint8 note, guint8 velocity)
{
  return backend_send_3_byte_message (backend, 0x80, channel, note, velocity);
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
backend_init_midi (struct backend *backend, const gchar *id)
{
  debug_print (1, "Initializing backend (%s) to '%s'...",
	       backend_name (), id);
  backend->type = BE_TYPE_MIDI;
  gint err = backend_init_int (backend, id);
  if (!err)
    {
      g_mutex_lock (&backend->mutex);
      backend_rx_drain (backend);
      g_mutex_unlock (&backend->mutex);
    }

  if (preferences_get_boolean (PREF_KEY_BE_STOP_WHEN_CONNECTING))
    {
      debug_print (1, "Stopping device...");
      if (backend_tx_raw (backend, (guint8 *) "\xfc", 1) < 0)
	{
	  error_print ("Error while stopping device");
	}
      usleep (BE_REST_TIME_US);
    }

  return err;
}

void
backend_destroy (struct backend *backend)
{
  debug_print (1, "Destroying backend...");

  if (backend->destroy_data)
    {
      backend->destroy_data (backend);
    }

  if (backend->type == BE_TYPE_MIDI)
    {
      backend_destroy_int (backend);
    }

  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  backend->destroy_data = NULL;
  backend->type = BE_TYPE_NONE;
  g_slist_free (backend->fs_ops);
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
      error_print ("Input port is NULL");
      return -ENOTCONN;
    }

  debug_print (4, "Reading data...");

  while (1)
    {
      if (!transfer->active)
	{
	  return -ECANCELED;
	}

      debug_print (6, "Checking timeout (%d ms, %d ms, %s mode)...",
		   transfer->time, transfer->timeout,
		   transfer->batch ? "batch" : "single");
      if (((transfer->batch && transfer->status == RECEIVING)
	   || !transfer->batch) && transfer->timeout > -1
	  && transfer->time >= transfer->timeout)
	{
	  debug_print (1, "Timeout (%d)", transfer->timeout);
	  gchar *text = debug_get_hex_data (debug_level, backend->buffer,
					    backend->rx_len);
	  debug_print (4, "Internal buffer data (%zd): %s", backend->rx_len,
		       text);
	  g_free (text);
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
	      debug_print (4, "Skipping non SysEx data (%zd): %s", rx_len,
			   text);
	      g_free (text);
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
      debug_print (3, "Queued data (%zu): %s", rx_len, text);
      g_free (text);
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
	  debug_print (4, "Reading from MIDI device...");
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
	  debug_print (4, "Reading from internal buffer...");
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
	      debug_print (4, "Skipping non SysEx data in buffer (%d): %s",
			   i, text);
	      g_free (text);
	    }
	  debug_print (3, "Copying %d bytes...", len - i);
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
	      debug_print (4, "Removing empty message...");
	      g_byte_array_remove_range (transfer->raw, 0, 2);
	      continue;
	    }

	  if (debug_level >= 4)
	    {
	      gchar *text =
		debug_get_hex_data (debug_level, transfer->raw->data,
				    transfer->raw->len);
	      debug_print (4, "Queued data (%d): %s",
			   transfer->raw->len, text);
	      g_free (text);
	    }
	}
      else
	{
	  debug_print (4, "No message in the queue. Continuing...");
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
	  debug_print (2, "Raw message received (%d): %s",
		       transfer->raw->len, text);
	  g_free (text);
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

  debug_print (2, "Draining buffers...");
  backend->rx_len = 0;
  backend_rx_drain_int (backend);
  while (!backend_rx_sysex (backend, &transfer))
    {
      free_msg (transfer.raw);
    }
}

enum path_type
backend_get_path_type (struct backend *backend)
{
  return (!backend || backend->type == BE_TYPE_SYSTEM) ? PATH_SYSTEM :
    PATH_INTERNAL;
}

GArray *
backend_get_devices ()
{
  struct backend_device *backend_device;
  GArray *devices =
    g_array_new (FALSE, FALSE, sizeof (struct backend_device));

  backend_device = g_malloc (sizeof (struct backend_device));
  backend_device->type = BE_TYPE_SYSTEM;
  snprintf (backend_device->id, LABEL_MAX, "%s", BE_SYSTEM_ID);
  snprintf (backend_device->name, LABEL_MAX, "%s", g_get_host_name ());
  g_array_append_vals (devices, backend_device, 1);

  backend_fill_devices_array (devices);
  return devices;
}

// A handshake function might return these values:
// 0, the device matches the connector.
// -ENODEV, the device does not match the connector but we can continue with the next connector.
// Other negative errors are allowed but we will not continue with the remaining connectors.

gint
backend_init_connector (struct backend *backend,
			struct backend_device *device,
			const gchar *conn_name,
			struct sysex_transfer *sysex_transfer)
{
  gint err;
  GSList *list = NULL, *iterator;
  gboolean active = TRUE;
  GSList *c;

  if (device->type == BE_TYPE_SYSTEM)
    {
      backend->conn_name = system_connector->name;
      backend->type = BE_TYPE_SYSTEM;
      return system_connector->handshake (backend);
    }

  err = backend_init_midi (backend, device->id);
  if (err)
    {
      return err;
    }

  c = connectors;
  while (c)
    {
      struct connector *connector = c->data;
      if (connector->regex)
	{
	  GRegex *regex = g_regex_new (connector->regex, G_REGEX_CASELESS,
				       0, NULL);
	  if (g_regex_match (regex, device->name, 0, NULL))
	    {
	      debug_print (1, "Connector %s matches the device",
			   connector->name);
	      list = g_slist_prepend (list, (void *) connector);
	    }
	  else
	    {
	      list = g_slist_append (list, (void *) connector);
	    }
	  g_regex_unref (regex);
	}
      else
	{
	  list = g_slist_append (list, (void *) connector);
	}
      c = c->next;
    }

  if (!conn_name)
    {
      backend_midi_handshake (backend);
    }

  err = -ENODEV;
  for (iterator = list; iterator; iterator = iterator->next)
    {
      const struct connector *c = iterator->data;

      if (sysex_transfer)
	{
	  g_mutex_lock (&sysex_transfer->mutex);
	  active = sysex_transfer->active;
	  g_mutex_unlock (&sysex_transfer->mutex);
	}

      if (!active)
	{
	  err = -ECANCELED;
	  goto end;
	}

      debug_print (1, "Testing %s connector (%sstandard handshake)...",
		   c->name, c->standard ? "" : "non ");

      if (conn_name)
	{
	  if (!strcmp (conn_name, c->name))
	    {
	      if (c->standard)
		{
		  backend_midi_handshake (backend);
		}
	      err = c->handshake (backend);
	      if (!err)
		{
		  debug_print (1, "Using %s connector...", c->name);
		  backend->conn_name = c->name;
		}
	      goto end;
	    }
	}
      else
	{
	  err = c->handshake (backend);
	  if (err && err != -ENODEV)
	    {
	      goto end;
	    }

	  if (!err)
	    {
	      debug_print (1, "Using %s connector...", c->name);
	      backend->conn_name = c->name;
	      goto end;
	    }
	}
    }

  error_print ("No device recognized");

end:
  g_slist_free (list);
  if (err)
    {
      backend_destroy (backend);
    }
  return err;
}

const struct preference PREF_BE_STOP_WHEN_CONNECTING = {
  .key = PREF_KEY_BE_STOP_WHEN_CONNECTING,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_true
};
