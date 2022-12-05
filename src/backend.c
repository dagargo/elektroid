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

void backend_destroy_midi (struct backend *);
gint backend_init_midi (struct backend *, const gchar *);
gboolean backend_check_midi (struct backend *);

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

  backend->device_desc.id = -1;
  backend->device_desc.storage = 0;
  backend->device_desc.name[0] = 0;
  backend->device_desc.alias[0] = 0;
  backend->device_name[0] = 0;
  backend->fs_ops = NULL;
  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  memset (&backend->midi_info, 0, sizeof (struct backend_midi_info));

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

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

	  snprintf (backend->device_name, LABEL_MAX,
		    "%02x-%02x-%02x %02x-%02x %02x-%02x %d.%d.%d.%d",
		    backend->midi_info.company[0],
		    backend->midi_info.company[1],
		    backend->midi_info.company[2],
		    backend->midi_info.family[0],
		    backend->midi_info.family[1],
		    backend->midi_info.model[0],
		    backend->midi_info.model[1],
		    backend->midi_info.version[0],
		    backend->midi_info.version[1],
		    backend->midi_info.version[2],
		    backend->midi_info.version[3]);
	  snprintf (backend->device_desc.name, LABEL_MAX, "%s",
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
backend_get_fs_ext (const struct device_desc *desc,
		    const struct fs_operations *ops)
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

  if ((size = backend_tx_raw (backend, (guint8 *) msg, 2)) < 0)
    {
      return size;
    }
  return 0;
}

gint
backend_init (struct backend *backend, const gchar * id)
{
  debug_print (1, "Initializing backend to '%s'...\n", id);
  backend->type = BE_TYPE_MIDI;
  return backend_init_midi (backend, id);
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
      backend_destroy_midi (backend);
    }

  backend->device_desc.id = -1;
  backend->device_desc.filesystems = 0;
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
      return backend_check_midi (backend);
    case BE_TYPE_SYSTEM:
      return TRUE;
    default:
      return FALSE;
    }
}
