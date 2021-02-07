/*
 *   connector.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
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

#include <stdio.h>
#include <math.h>
#include <netinet/in.h>
#include <byteswap.h>
#include <sys/poll.h>
#include <zlib.h>
#include "connector.h"
#include "utils.h"

#define KB 1024
#define BUFF_SIZE (4 * KB)
#define RING_BUFF_SIZE (256 * KB)
#define TRANSF_BLOCK_SIZE_SAMPLE 0x2000
#define TRANSF_BLOCK_SIZE_OS 0x800
#define POLL_TIMEOUT 20
#define REST_TIME 50000

static const guint8 MSG_HEADER[] = { 0xf0, 0, 0x20, 0x3c, 0x10, 0 };

static const guint8 INQ_DEVICE[] = { 0x1 };
static const guint8 INQ_VERSION[] = { 0x2 };
static const guint8 INQ_UID[] = { 0x3 };
static const guint8 INQ_LS_DIR_TEMPLATE[] = { 0x10 };
static const guint8 INQ_NEW_DIR_TEMPLATE[] = { 0x11 };
static const guint8 INQ_DELETE_DIR_TEMPLATE[] = { 0x12 };
static const guint8 INQ_DELETE_FILE_TEMPLATE[] = { 0x20 };
static const guint8 INQ_RENAME_TEMPLATE[] = { 0x21 };
static const guint8 INQ_INFO_FILE_TEMPLATE[] = { 0x30 };
static const guint8 INQ_DWL_TEMPLATE[] =
  { 0x32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 INQ_DWL_TEMPLATE_END[] = { 0x31 };
static const guint8 INQ_NEW_TEMPLATE[] = { 0x40, 0, 0, 0, 0 };

static const guint8 INQ_UPL_TEMPLATE_1ST[] =
  { 0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0xbb, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0x7f,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0
};
static const guint8 INQ_UPL_TEMPLATE_NTH[] =
  { 0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 INQ_UPL_TEMPLATE_END[] = { 0x41, 0, 0, 0, 0, 0, 0, 0, 0 };

static const guint8 INQ_OS_UPGRADE_START[] =
  { 0x50, 0, 0, 0, 0, 's', 'y', 's', 'e', 'x', '\0', 1 };
static const guint8 INQ_OS_UPGRADE_WRITE[] =
  { 0x51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static guchar
connector_get_msg_status (const GByteArray * msg)
{
  return msg->data[5];
}

static gchar *
connector_get_msg_string (const GByteArray * msg)
{
  return (gchar *) & msg->data[6];
}

void
free_msg (gpointer msg)
{
  g_byte_array_free ((GByteArray *) msg, TRUE);
}

void
connector_free_dir_iterator (struct connector_dir_iterator *d_iter)
{
  free_msg (d_iter->msg);
  free (d_iter);
}

struct connector_dir_iterator *
connector_new_dir_iterator (GByteArray * msg)
{
  struct connector_dir_iterator *dir_iterator =
    malloc (sizeof (struct connector_dir_iterator));

  dir_iterator->dentry = NULL;
  dir_iterator->msg = msg;
  dir_iterator->pos = 5;

  return dir_iterator;
}

guint
connector_get_next_dentry (struct connector_dir_iterator *dir_iterator)
{
  uint32_t *data;
  gchar *dentry_cp1252;

  if (dir_iterator->dentry != NULL)
    {
      g_free (dir_iterator->dentry);
    }

  if (dir_iterator->pos == dir_iterator->msg->len)
    {
      dir_iterator->dentry = NULL;
      return -ENOENT;
    }
  else
    {
      data = (uint32_t *) & dir_iterator->msg->data[dir_iterator->pos];
      dir_iterator->cksum = ntohl (*data);

      dir_iterator->pos += 4;
      data = (uint32_t *) & dir_iterator->msg->data[dir_iterator->pos];
      dir_iterator->size = ntohl (*data);

      dir_iterator->pos += 5;
      dir_iterator->type = dir_iterator->msg->data[dir_iterator->pos];

      dir_iterator->pos++;
      dentry_cp1252 = (gchar *) & dir_iterator->msg->data[dir_iterator->pos];
      dir_iterator->dentry =
	g_convert (dentry_cp1252, -1, "UTF8", "CP1252", NULL, NULL, NULL);

      while (dir_iterator->pos < dir_iterator->msg->len
	     && dir_iterator->msg->data[dir_iterator->pos] != 0)
	{

	  dir_iterator->pos++;
	}

      dir_iterator->pos++;

      return 0;
    }
}

static GByteArray *
connector_sysex_to_msg (const GByteArray * src)
{
  GByteArray *dst;
  int i, j, k, dst_len;
  unsigned int shift;

  dst_len = src->len - ceill (src->len / 8.0);
  dst = g_byte_array_new ();
  g_byte_array_set_size (dst, dst_len);

  for (i = 0, j = 0; i < src->len; i += 8, j += 7)
    {
      shift = 0x40;
      for (k = 0; k < 7 && i + k + 1 < src->len; k++)
	{
	  dst->data[j + k] =
	    src->data[i + k + 1] | (src->data[i] & shift ? 0x80 : 0);
	  shift = shift >> 1;
	}
    }

  return dst;
}

static GByteArray *
connector_msg_to_sysex (const GByteArray * src)
{
  GByteArray *dst;
  int i, j, k, dst_len;
  unsigned int accum;

  dst_len = src->len + ceill (src->len / 7.0);
  dst = g_byte_array_new ();
  g_byte_array_set_size (dst, dst_len);

  for (i = 0, j = 0; j < src->len; i += 8, j += 7)
    {
      accum = 0;
      for (k = 0; k < 7; k++)
	{
	  accum = accum << 1;
	  if (j + k < src->len)
	    {
	      if (src->data[j + k] & 0x80)
		{
		  accum |= 1;
		}
	      dst->data[i + k + 1] = src->data[j + k] & 0x7f;
	    }
	}
      dst->data[i] = accum;
    }

  return dst;
}

void
connector_get_sample_info_from_msg (GByteArray * info_msg, gint * id,
				    guint * size)
{
  if (!connector_get_msg_status (info_msg))
    {
      if (id)
	{
	  *id = -1;
	}
    }
  else
    {
      if (id)
	{
	  *id = ntohl (*((uint32_t *) & info_msg->data[6]));
	}
      if (size)
	{
	  *size = ntohl (*((uint32_t *) & info_msg->data[10]));
	}
    }
}

static GByteArray *
connector_new_msg_data (const guint8 * data, guint len)
{
  GByteArray *msg = g_byte_array_new ();

  g_byte_array_append (msg, (guchar *) "\0\0\0\0", 4);
  g_byte_array_append (msg, data, len);

  return msg;
}

static GByteArray *
connector_new_msg_path (const guint8 * data, guint len, const gchar * path)
{
  GByteArray *msg = connector_new_msg_data (data, len);

  g_byte_array_append (msg, (guchar *) path, strlen (path));
  g_byte_array_append (msg, (guchar *) "\0", 1);

  return msg;
}

static GByteArray *
connector_new_msg_dir_list (const gchar * path)
{
  return connector_new_msg_path (INQ_LS_DIR_TEMPLATE,
				 sizeof (INQ_LS_DIR_TEMPLATE), path);
}

static GByteArray *
connector_new_msg_info_file (const gchar * path)
{
  return connector_new_msg_path (INQ_INFO_FILE_TEMPLATE,
				 sizeof (INQ_INFO_FILE_TEMPLATE), path);
}

static GByteArray *
connector_new_msg_new_dir (const gchar * path)
{
  return connector_new_msg_path (INQ_NEW_DIR_TEMPLATE,
				 sizeof (INQ_NEW_DIR_TEMPLATE), path);
}

static GByteArray *
connector_new_msg_end_download (gint id)
{
  uint32_t aux32;
  GByteArray *msg = connector_new_msg_data (INQ_DWL_TEMPLATE_END,
					    sizeof (INQ_DWL_TEMPLATE_END));

  aux32 = htonl (id);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (uint32_t));
  return msg;
}

static GByteArray *
connector_new_msg_new_upload (const gchar * path, guint frames)
{
  uint32_t aux32;
  GByteArray *msg = connector_new_msg_path (INQ_NEW_TEMPLATE,
					    sizeof (INQ_NEW_TEMPLATE), path);

  aux32 = htonl ((frames + 32) * 2);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_upl_blck (guint id, gshort ** data, guint frames,
			    ssize_t * total, guint seq)
{
  uint32_t aux32;
  uint16_t aux16;
  int i, consumed, frames_blck;
  GByteArray *msg;

  if (seq == 0)
    {
      msg = connector_new_msg_data (INQ_UPL_TEMPLATE_1ST,
				    sizeof (INQ_UPL_TEMPLATE_1ST));
      frames_blck = 4064;
    }
  else
    {
      msg = connector_new_msg_data (INQ_UPL_TEMPLATE_NTH,
				    sizeof (INQ_UPL_TEMPLATE_NTH));
      frames_blck = 4096;
    }

  i = 0;
  consumed = 0;
  while (i < frames_blck && *total < frames)
    {
      aux16 = htons (**data);
      g_byte_array_append (msg, (guchar *) & aux16, sizeof (uint16_t));
      (*data)++;
      (*total)++;
      consumed++;
      i++;
    }

  aux32 = htonl (id);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));

  if (seq == 0)
    {
      aux32 = htonl ((consumed + 32) * 2);
      memcpy (&msg->data[9], &aux32, sizeof (uint32_t));
      aux32 = htonl (frames * sizeof (gshort));
      memcpy (&msg->data[21], &aux32, sizeof (uint32_t));
      aux32 = htonl (frames - 1);
      memcpy (&msg->data[33], &aux32, sizeof (uint32_t));
    }
  else
    {
      aux32 = htonl (consumed * 2);
      memcpy (&msg->data[9], &aux32, sizeof (uint32_t));
      aux32 = htonl (0x2000 * seq);
      memcpy (&msg->data[13], &aux32, sizeof (uint32_t));
    }

  return msg;
}

static GByteArray *
connector_new_msg_upl_end (guint id, guint frames)
{
  uint32_t aux32;
  GByteArray *msg = connector_new_msg_data (INQ_UPL_TEMPLATE_END,
					    sizeof (INQ_UPL_TEMPLATE_END));

  aux32 = htonl (id);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));
  aux32 = htonl ((frames + 32) * 2);
  memcpy (&msg->data[9], &aux32, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_dwnl_blck (guint id, guint start, guint size)
{
  uint32_t aux;
  GByteArray *msg =
    connector_new_msg_data (INQ_DWL_TEMPLATE, sizeof (INQ_DWL_TEMPLATE));

  aux = htonl (id);
  memcpy (&msg->data[5], &aux, sizeof (uint32_t));
  aux = htonl (size);
  memcpy (&msg->data[9], &aux, sizeof (uint32_t));
  aux = htonl (start);
  memcpy (&msg->data[13], &aux, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_get_msg_payload (GByteArray * msg)
{
  GByteArray *transformed;
  GByteArray *payload;
  gint len = msg->len - sizeof (MSG_HEADER) - 1;

  if (len > 0)
    {
      payload = g_byte_array_new ();
      g_byte_array_append (payload, &msg->data[sizeof (MSG_HEADER)], len);
      transformed = connector_sysex_to_msg (payload);
      free_msg (payload);
    }
  else
    {
      transformed = NULL;
    }

  return transformed;
}

static ssize_t
connector_tx_raw (struct connector *connector, const guint8 * data, guint len)
{
  ssize_t tx_len;

  if (!connector->outputp)
    {
      error_print ("Output port is NULL\n");
      return -1;
    }

  snd_rawmidi_read (connector->inputp, NULL, 0);	// trigger reading

  tx_len = snd_rawmidi_write (connector->outputp, data, len);
  if (tx_len < 0)
    {
      error_print ("Error while sending message. %s.\n", g_strerror (errno));
      connector_destroy (connector);
      return tx_len;
    }
  return tx_len;
}

ssize_t
connector_tx_sysex (struct connector *connector, GByteArray * data,
		    struct connector_sysex_transfer *transfer)
{
  ssize_t tx_len;
  guint total;
  guint len;
  guchar *b;
  ssize_t ret = data->len;

  transfer->status = SENDING;

  b = data->data;
  total = 0;
  while (total < data->len && transfer->active)
    {
      len = data->len - total;
      if (len > BUFF_SIZE)
	{
	  len = BUFF_SIZE;
	}

      tx_len = connector_tx_raw (connector, b, len);
      if (tx_len < 0)
	{
	  ret = tx_len;
	  break;
	}
      b += len;
      total += len;
    }

  transfer->active = FALSE;
  transfer->status = FINISHED;
  return ret;
}

static ssize_t
connector_tx (struct connector *connector, const GByteArray * msg)
{
  ssize_t ret;
  uint16_t aux;
  GByteArray *sysex;
  GByteArray *data;
  struct connector_sysex_transfer transfer;
  gchar *text;

  aux = htons (connector->seq);
  memcpy (msg->data, &aux, sizeof (uint16_t));
  if (connector->seq == USHRT_MAX)
    {
      connector->seq = 0;
    }
  else
    {
      connector->seq++;
    }

  transfer.active = TRUE;
  data = g_byte_array_new ();
  g_byte_array_append (data, MSG_HEADER, sizeof (MSG_HEADER));
  sysex = connector_msg_to_sysex (msg);
  g_byte_array_append (data, sysex->data, sysex->len);
  free_msg (sysex);
  g_byte_array_append (data, (guint8 *) "\xf7", 1);

  ret = connector_tx_sysex (connector, data, &transfer);

  if (ret >= 0)
    {
      if (debug_level > 1)
	{
	  text = debug_get_hex_msg (data);
	  debug_print (2, "Raw message sent (%d): %s\n", data->len, text);
	  free (text);
	}

      text = debug_get_hex_msg (msg);
      debug_print (1, "Message sent (%d): %s\n", msg->len, text);
      free (text);
    }

  free_msg (data);
  return ret;
}

void
connector_rx_drain (struct connector *connector)
{
  debug_print (2, "Draining buffer...\n");
  connector->rx_len = 0;
  snd_rawmidi_drain (connector->inputp);
}

static gboolean
connector_is_rt_msg (guint8 * data, guint len)
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
connector_rx_raw (struct connector *connector, guint8 * data, guint len,
		  struct connector_sysex_transfer *transfer)
{
  ssize_t rx_len;
  guint total_time;
  unsigned short revents;
  gint err;
  gchar *text;

  if (!connector->inputp)
    {
      error_print ("Input port is NULL\n");
      return -1;
    }

  total_time = 0;

  while (1)
    {
      err = poll (connector->pfds, connector->npfds, POLL_TIMEOUT);

      if (!transfer->active)
	{
	  return -ENODATA;
	}

      if (err == 0)
	{
	  total_time += POLL_TIMEOUT;
	  if (((transfer->batch && transfer->status == RECEIVING)
	       || !transfer->batch) && transfer->timeout > -1
	      && total_time >= transfer->timeout)
	    {
	      debug_print (1, "Timeout!\n");
	      return -ENODATA;
	    }
	  continue;
	}

      if (err < 0)
	{
	  error_print ("Error while polling. %s.\n", g_strerror (errno));
	  connector_destroy (connector);
	  return err;
	}

      if ((err =
	   snd_rawmidi_poll_descriptors_revents (connector->inputp,
						 connector->pfds,
						 connector->npfds,
						 &revents)) < 0)
	{
	  error_print ("Error while getting poll events. %s.\n",
		       g_strerror (errno));
	  connector_destroy (connector);
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

      rx_len = snd_rawmidi_read (connector->inputp, data, len);

      if (rx_len == -EAGAIN || rx_len == 0)
	{
	  continue;
	}

      if (rx_len > 0)
	{
	  if (connector_is_rt_msg (data, rx_len))
	    {
	      continue;
	    }
	  break;
	}

      if (rx_len < 0)
	{
	  error_print ("Error while receiving message. %s.\n",
		       g_strerror (errno));
	  connector_destroy (connector);
	  break;
	}

    }

  if (debug_level > 1)
    {
      text = debug_get_hex_data (3, data, rx_len);
      debug_print (1, "Buffer content (%zu): %s\n", rx_len, text);
      free (text);
    }

  return rx_len;
}

GByteArray *
connector_rx_sysex (struct connector *connector,
		    struct connector_sysex_transfer *transfer)
{
  gint i;
  guint8 *b;
  GByteArray *sysex = g_byte_array_new ();

  transfer->status = WAITING;

  i = 0;
  if (connector->rx_len < 0)
    {
      connector->rx_len = 0;
    }
  b = connector->buffer;

  while (1)
    {
      if (i == connector->rx_len)
	{
	  connector->rx_len =
	    connector_rx_raw (connector, connector->buffer, BUFF_SIZE,
			      transfer);

	  if (connector->rx_len == -ENODATA)
	    {
	      goto error;
	    }

	  if (connector->rx_len < 0)
	    {
	      goto error;
	    }

	  b = connector->buffer;
	  i = 0;
	}

      while (i < connector->rx_len && *b != 0xf0)
	{
	  b++;
	  i++;
	}

      if (i < connector->rx_len)
	{
	  break;
	}
    }

  g_byte_array_append (sysex, b, 1);
  b++;
  i++;
  transfer->status = RECEIVING;

  while (1)
    {
      if (i == connector->rx_len)
	{
	  connector->rx_len =
	    connector_rx_raw (connector, connector->buffer, BUFF_SIZE,
			      transfer);

	  if (connector->rx_len == -ENODATA && transfer->batch)
	    {
	      break;
	    }

	  if (connector->rx_len < 0)
	    {
	      goto error;
	    }

	  b = connector->buffer;
	  i = 0;
	}

      while (i < connector->rx_len && (*b != 0xf7 || transfer->batch))
	{
	  if (!connector_is_rt_msg (b, 1))
	    {
	      g_byte_array_append (sysex, b, 1);
	    }
	  b++;
	  i++;
	}

      if (i < connector->rx_len)
	{
	  g_byte_array_append (sysex, b, 1);
	  connector->rx_len = connector->rx_len - i - 1;
	  if (connector->rx_len > 0)
	    {
	      memmove (connector->buffer, &connector->buffer[i + 1],
		       connector->rx_len);
	    }
	  break;
	}

    }

  goto end;

error:
  free_msg (sysex);
  sysex = NULL;
end:
  transfer->active = FALSE;
  transfer->status = FINISHED;
  return sysex;
}

static GByteArray *
connector_rx (struct connector *connector)
{
  GByteArray *msg;
  GByteArray *sysex;
  struct connector_sysex_transfer transfer;
  gchar *text;

  transfer.active = TRUE;
  transfer.timeout = SYSEX_TIMEOUT;
  transfer.batch = FALSE;

  sysex = connector_rx_sysex (connector, &transfer);
  if (!sysex)
    {
      return NULL;
    }
  while (sysex->len < 12
	 || (sysex->len >= 12
	     && (sysex->data[0] != MSG_HEADER[0]
		 || sysex->data[1] != MSG_HEADER[1]
		 || sysex->data[2] != MSG_HEADER[2]
		 || sysex->data[3] != MSG_HEADER[3]
		 || sysex->data[4] != MSG_HEADER[4]
		 || sysex->data[5] != MSG_HEADER[5])))
    {
      if (debug_level > 1)
	{
	  text = debug_get_hex_msg (sysex);
	  debug_print (2, "Message skipped (%d): %s\n", sysex->len, text);
	  free (text);
	}
      free_msg (sysex);

      transfer.active = TRUE;
      sysex = connector_rx_sysex (connector, &transfer);
      if (!sysex)
	{
	  return NULL;
	}
    }

  if (debug_level > 1)
    {
      text = debug_get_hex_msg (sysex);
      debug_print (2, "Raw message received (%d): %s\n", sysex->len, text);
      free (text);
    }

  msg = connector_get_msg_payload (sysex);
  if (msg)
    {
      text = debug_get_hex_msg (msg);
      debug_print (1, "Message received (%d): %s\n", msg->len, text);
      free (text);
    }

  free_msg (sysex);
  return msg;
}

static GByteArray *
connector_tx_and_rx (struct connector *connector, GByteArray * tx_msg)
{
  ssize_t len;
  GByteArray *rx_msg;

  g_mutex_lock (&connector->mutex);

  connector_rx_drain (connector);

  len = connector_tx (connector, tx_msg);
  if (len < 0)
    {
      rx_msg = NULL;
      goto cleanup;
    }

  rx_msg = connector_rx (connector);

cleanup:
  free_msg (tx_msg);
  g_mutex_unlock (&connector->mutex);
  return rx_msg;
}

struct connector_dir_iterator *
connector_read_dir (struct connector *connector, const gchar * dir)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gchar *dir_cp1252 = g_convert (dir, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!dir_cp1252)
    {
      return NULL;
    }

  tx_msg = connector_new_msg_dir_list (dir_cp1252);
  g_free (dir_cp1252);

  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      return NULL;
    }

  return connector_new_dir_iterator (rx_msg);
}

gint
connector_rename (struct connector *connector, const gchar * old,
		  const gchar * new)
{
  gint res;
  GByteArray *rx_msg;
  GByteArray *tx_msg = connector_new_msg_data (INQ_RENAME_TEMPLATE,
					       sizeof (INQ_RENAME_TEMPLATE));
  gchar *new_cp1252 = g_convert (new, -1, "CP1252", "UTF8", NULL, NULL, NULL);
  if (!new_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  gchar *old_cp1252 = g_convert (old, -1, "CP1252", "UTF8", NULL, NULL, NULL);
  if (!old_cp1252)
    {
      g_free (new_cp1252);
      errno = EINVAL;
      return -1;
    }

  g_byte_array_append (tx_msg, (guchar *) old_cp1252, strlen (old_cp1252));
  g_byte_array_append (tx_msg, (guchar *) "\0", 1);
  g_byte_array_append (tx_msg, (guchar *) new_cp1252, strlen (new_cp1252));
  g_byte_array_append (tx_msg, (guchar *) "\0", 1);

  g_free (old_cp1252);
  g_free (new_cp1252);

  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      errno = EIO;
      return -1;
    }
  //Response: x, x, x, x, 0xa1, [0 (error), 1 (success)]...
  if (connector_get_msg_status (rx_msg))
    {
      res = 0;
    }
  else
    {
      res = -1;
      errno = EPERM;
      error_print ("%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return res;
}

static gint
connector_delete (struct connector *connector, const gchar * path,
		  const guint8 * template, gint size)
{
  gint res;
  GByteArray *rx_msg;
  GByteArray *tx_msg;
  gchar *path_cp1252 =
    g_convert (path, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!path_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  tx_msg = connector_new_msg_path (template, size, path_cp1252);
  g_free (path_cp1252);

  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      errno = EIO;
      return -1;
    }
  //Response: x, x, x, x, 0xX0, [0 (error), 1 (success)]...
  if (connector_get_msg_status (rx_msg))
    {
      res = 0;
    }
  else
    {
      res = -1;
      errno = EPERM;
      error_print ("%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return res;
}

gint
connector_delete_file (struct connector *connector, const gchar * path)
{
  return connector_delete (connector, path, INQ_DELETE_FILE_TEMPLATE,
			   sizeof (INQ_DELETE_FILE_TEMPLATE));
}

gint
connector_delete_dir (struct connector *connector, const gchar * path)
{
  return connector_delete (connector, path, INQ_DELETE_DIR_TEMPLATE,
			   sizeof (INQ_DELETE_DIR_TEMPLATE));
}

static gint
connector_create_upload (struct connector *connector, const gchar * path,
			 guint fsize)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint id;
  gchar *path_cp1252 =
    g_convert (path, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!path_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  tx_msg = connector_new_msg_new_upload (path_cp1252, fsize);
  g_free (path_cp1252);
  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      errno = EIO;
      return -1;
    }

  //Response: x, x, x, x, 0xc0, [0 (error), 1 (success)], id, frames
  connector_get_sample_info_from_msg (rx_msg, &id, NULL);
  if (id < 0)
    {
      errno = EEXIST;
      error_print ("%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return id;
}

ssize_t
connector_upload (struct connector *connector, GArray * sample,
		  gchar * path, struct connector_sample_transfer *transfer,
		  void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t transferred;
  gshort *data;
  gint id;
  int i;
  gboolean active;

  //TODO: check if the file already exists? (Device makes no difference between creating a new file and creating an already existent file. The new file would be deleted if an upload is not sent, though.)
  //TODO: limit sample upload?

  id = connector_create_upload (connector, path, sample->len);
  if (id < 0)
    {
      return -1;
    }

  data = (gshort *) sample->data;
  transferred = 0;
  i = 0;
  g_mutex_lock (&transfer->mutex);
  active = (!transfer || transfer->active);
  g_mutex_unlock (&transfer->mutex);
  while (transferred < sample->len && active)
    {
      if (progress)
	{
	  progress (transferred / (double) sample->len);
	}

      tx_msg =
	connector_new_msg_upl_blck (id, &data, sample->len, &transferred, i);
      rx_msg = connector_tx_and_rx (connector, tx_msg);
      if (!rx_msg)
	{
	  return -1;
	}
      //Response: x, x, x, x, 0xc2, [0 (error), 1 (success)]...
      if (!connector_get_msg_status (rx_msg))
	{
	  error_print ("Unexpected status\n");
	}
      free_msg (rx_msg);
      i++;
      g_mutex_lock (&transfer->mutex);
      active = (!transfer || transfer->active);
      g_mutex_unlock (&transfer->mutex);

      usleep (REST_TIME);
    }

  debug_print (2, "%zu frames sent\n", transferred);

  if (active)
    {
      if (progress)
	{
	  progress (transferred / (double) sample->len);
	}

      tx_msg = connector_new_msg_upl_end (id, transferred);
      rx_msg = connector_tx_and_rx (connector, tx_msg);
      if (!rx_msg)
	{
	  return -1;
	}
      //Response: x, x, x, x, 0xc1, [0 (error), 1 (success)]...
      if (!connector_get_msg_status (rx_msg))
	{
	  error_print ("Unexpected status\n");
	}
      free_msg (rx_msg);
    }

  return active ? transferred : -1;
}

GArray *
connector_download (struct connector *connector, const gchar * path,
		    struct connector_sample_transfer *transfer,
		    void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *data;
  gint id;
  guint frames;
  guint next_block_start;
  guint req_size;
  int offset;
  int16_t v;
  int16_t *frame;
  int i;
  gboolean active;
  GArray *result;
  gchar *path_cp1252 =
    g_convert (path, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!path_cp1252)
    {
      return NULL;
    }

  tx_msg = connector_new_msg_info_file (path_cp1252);
  g_free (path_cp1252);
  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      return NULL;
    }
  connector_get_sample_info_from_msg (rx_msg, &id, &frames);
  free_msg (rx_msg);
  if (id < 0)
    {
      error_print ("File %s not found\n", path);
      return NULL;
    }

  debug_print (2, "%d frames to download\n", frames);

  data = g_byte_array_new ();

  next_block_start = 0;
  offset = 64;
  g_mutex_lock (&transfer->mutex);
  active = (!transfer || transfer->active);
  g_mutex_unlock (&transfer->mutex);
  while (next_block_start < frames && active)
    {
      if (progress)
	{
	  progress (next_block_start / (double) frames);
	}

      req_size =
	frames - next_block_start >
	TRANSF_BLOCK_SIZE_SAMPLE ? TRANSF_BLOCK_SIZE_SAMPLE : frames -
	next_block_start;
      tx_msg = connector_new_msg_dwnl_blck (id, next_block_start, req_size);
      rx_msg = connector_tx_and_rx (connector, tx_msg);
      if (!rx_msg)
	{
	  result = NULL;
	  goto cleanup;
	}
      g_byte_array_append (data, &rx_msg->data[22 + offset],
			   req_size - offset);
      free_msg (rx_msg);

      next_block_start += req_size;
      offset = 0;
      g_mutex_lock (&transfer->mutex);
      active = (!transfer || transfer->active);
      g_mutex_unlock (&transfer->mutex);

      usleep (REST_TIME);
    }

  debug_print (2, "%d bytes received\n", next_block_start);

  if (active)
    {
      if (progress)
	{
	  progress (next_block_start / (double) frames);
	}

      result = g_array_new (FALSE, FALSE, sizeof (short));
      frame = (gshort *) data->data;
      for (i = 0; i < data->len; i += 2)
	{
	  v = ntohs (*frame);
	  g_array_append_val (result, v);
	  frame++;
	}
    }
  else
    {
      result = NULL;
    }

  tx_msg = connector_new_msg_end_download (id);
  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      if (result)
	{
	  g_array_free (result, TRUE);
	  result = NULL;
	}
      goto cleanup;
    }
  //Response: x, x, x, x, 0xb1, 00 00 00 0a 00 01 65 de (sample id and received bytes)
  free_msg (rx_msg);

cleanup:
  free_msg (data);
  return result;
}

gint
connector_create_dir (struct connector *connector, const gchar * path)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint res;
  gchar *path_cp1252 =
    g_convert (path, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!path_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  tx_msg = connector_new_msg_new_dir (path_cp1252);
  g_free (path_cp1252);
  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      errno = EIO;
      return -1;
    }
  //Response: x, x, x, x, 0x91, [0 (error), 1 (success)]...
  if (connector_get_msg_status (rx_msg))
    {
      res = 0;
    }
  else
    {
      res = -1;
      errno = EEXIST;
      error_print ("%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return res;
}

static GByteArray *
connector_new_msg_upgrade_os_start (guint size)
{
  GByteArray *msg = connector_new_msg_data (INQ_OS_UPGRADE_START,
					    sizeof (INQ_OS_UPGRADE_START));

  memcpy (&msg->data[5], &size, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_upgrade_os_write (GByteArray * os_data, gint * offset)
{
  GByteArray *msg = connector_new_msg_data (INQ_OS_UPGRADE_WRITE,
					    sizeof (INQ_OS_UPGRADE_WRITE));
  guint len;
  uint32_t crc;
  uint32_t aux32;

  if (*offset + TRANSF_BLOCK_SIZE_OS < os_data->len)
    {
      len = TRANSF_BLOCK_SIZE_OS;
    }
  else
    {
      len = os_data->len - *offset;
    }

  crc = crc32 (0xffffffff, &os_data->data[*offset], len);

  printf ("%0x\n", crc);

  aux32 = htonl (crc);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));
  aux32 = htonl (len);
  memcpy (&msg->data[9], &aux32, sizeof (uint32_t));
  aux32 = htonl (*offset);
  memcpy (&msg->data[13], &aux32, sizeof (uint32_t));

  g_byte_array_append (msg, &os_data->data[*offset], len);

  *offset = *offset + len;

  return msg;
}

gint
connector_upgrade_os (struct connector *connector, GByteArray * data,
		      struct connector_sysex_transfer *transfer)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint8 op;
  gint offset;
  gint res = 0;

  transfer->status = SENDING;

  tx_msg = connector_new_msg_upgrade_os_start (data->len);
  rx_msg = connector_tx_and_rx (connector, tx_msg);

  if (!rx_msg)
    {
      errno = EIO;
      res = -1;
      goto end;
    }
  //Response: x, x, x, x, 0xd1, [0 (ok), 1 (error)]...
  op = connector_get_msg_status (rx_msg);
  if (op)
    {
      res = -1;
      errno = EIO;
      error_print ("%s (%s)\n", g_strerror (errno),
		   connector_get_msg_string (rx_msg));
      goto cleanup;
    }

  free_msg (rx_msg);

  offset = 0;
  while (offset < data->len)
    {
      tx_msg = connector_new_msg_upgrade_os_write (data, &offset);
      rx_msg = connector_tx_and_rx (connector, tx_msg);

      if (!rx_msg)
	{
	  errno = EIO;
	  res = -1;
	  goto end;
	}
      //Response: x, x, x, x, 0xd1, int32, [0..3]...
      op = rx_msg->data[9];
      if (op == 1)
	{
	  break;
	}
      else if (op > 1)
	{
	  res = -1;
	  errno = EIO;
	  error_print ("%s (%s)\n", g_strerror (errno),
		       connector_get_msg_string (rx_msg));
	  goto cleanup;
	}

      free_msg (rx_msg);

      usleep (REST_TIME);
    }

cleanup:
  free_msg (rx_msg);
end:
  transfer->active = FALSE;
  transfer->status = FINISHED;
  return res;
}

void
connector_destroy (struct connector *connector)
{
  int err;

  debug_print (1, "Destroying connector...\n");

  if (connector->inputp)
    {
      err = snd_rawmidi_close (connector->inputp);
      if (err)
	{
	  error_print ("Error while closing MIDI port: %s\n",
		       g_strerror (errno));
	}
      connector->inputp = NULL;
    }

  if (connector->outputp)
    {
      err = snd_rawmidi_close (connector->outputp);
      if (err)
	{
	  error_print ("Error while closing MIDI port: %s\n",
		       g_strerror (errno));
	}
      connector->outputp = NULL;
    }

  if (connector->device_name)
    {
      free (connector->device_name);
      connector->device_name = NULL;
    }

  if (connector->buffer)
    {
      free (connector->buffer);
      connector->buffer = NULL;
    }

  if (connector->pfds)
    {
      free (connector->pfds);
      connector->pfds = NULL;
    }
}

static const gchar *
connector_get_device_name (gint device_id)
{
  switch (device_id)
    {
    case 0x8:
      return "Analog Rytm";
    case 0xc:
      return "Digitakt";
    case 0x10:
      return "Analog Rytm MKII";
    case 0x19:
      return "Model:Samples";
    default:
      return "-";
    }
}

gint
connector_init (struct connector *connector, gint card)
{
  int err;
  GByteArray *tx_msg;
  GByteArray *rx_msg_device;
  GByteArray *rx_msg_fw_ver;
  GByteArray *rx_msg_uid;
  snd_rawmidi_params_t *params;
  gchar name[32];
  sprintf (name, "hw:%d", card);

  connector->inputp = NULL;
  connector->outputp = NULL;
  connector->device_name = NULL;
  connector->buffer = NULL;
  connector->rx_len = 0;
  connector->pfds = NULL;

  if (card < 0)
    {
      debug_print (1, "Invalid card\n");
      err = -1;
      goto cleanup;
    }

  debug_print (1, "Initializing connector to '%s'...\n", name);

  if ((err =
       snd_rawmidi_open (&connector->inputp, &connector->outputp,
			 name, SND_RAWMIDI_NONBLOCK | SND_RAWMIDI_SYNC)) < 0)
    {
      error_print ("Error while opening MIDI port: %s\n", g_strerror (errno));
      goto cleanup;
    }

  debug_print (1, "Setting blocking mode...\n");
  if ((err = snd_rawmidi_nonblock (connector->outputp, 0)) < 0)
    {
      error_print ("Error while setting blocking mode\n");
      goto cleanup;
    }
  if ((err = snd_rawmidi_nonblock (connector->inputp, 1)) < 0)
    {
      error_print ("Error while setting blocking mode\n");
      goto cleanup;
    }

  debug_print (1, "Stopping device...\n");
  if (snd_rawmidi_write (connector->outputp, "\xfc", 1) < 0)
    {
      error_print ("Error while stopping device\n");
    }

  connector->seq = 0;
  connector->device_name = malloc (LABEL_MAX);
  if (!connector->device_name)
    {
      goto cleanup;
    }

  connector->buffer = malloc (sizeof (guint8) * BUFF_SIZE);
  if (!connector->buffer)
    {
      goto cleanup;
    }

  connector->npfds = snd_rawmidi_poll_descriptors_count (connector->inputp);
  connector->pfds = malloc (connector->npfds * sizeof (struct pollfd));
  if (!connector->buffer)
    {
      goto cleanup;
    }
  snd_rawmidi_poll_descriptors (connector->inputp, connector->pfds,
				connector->npfds);

  err = snd_rawmidi_params_malloc (&params);
  if (err)
    {
      goto cleanup;
    }

  err = snd_rawmidi_params_current (connector->inputp, params);
  if (err)
    {
      goto cleanup_params;
    }

  err =
    snd_rawmidi_params_set_buffer_size (connector->inputp, params,
					RING_BUFF_SIZE);
  if (err)
    {
      goto cleanup_params;
    }

  err = snd_rawmidi_params (connector->inputp, params);
  if (err)
    {
      goto cleanup_params;
    }

  tx_msg = connector_new_msg_data (INQ_DEVICE, sizeof (INQ_DEVICE));
  rx_msg_device = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg_device)
    {
      err = -1;
      goto cleanup;
    }

  tx_msg = connector_new_msg_data (INQ_VERSION, sizeof (INQ_VERSION));
  rx_msg_fw_ver = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg_fw_ver)
    {
      err = -1;
      goto cleanup_device;
    }

  if (debug_level)
    {
      tx_msg = connector_new_msg_data (INQ_UID, sizeof (INQ_UID));
      rx_msg_uid = connector_tx_and_rx (connector, tx_msg);
      if (rx_msg_uid)
	{
	  debug_print (1, "UID: %x\n", *((guint32 *) & rx_msg_uid->data[5]));
	  free_msg (rx_msg_uid);
	}
    }

  snprintf (connector->device_name, LABEL_MAX, "%s %s (%s)",
	    connector_get_device_name (rx_msg_device->data[5]),
	    &rx_msg_fw_ver->data[10],
	    &rx_msg_device->data[7 + rx_msg_device->data[6]]);

  debug_print (1, "Connected to %s\n", connector->device_name);

  err = 0;

  free_msg (rx_msg_fw_ver);
cleanup_device:
  free_msg (rx_msg_device);
cleanup_params:
  snd_rawmidi_params_free (params);
cleanup:
  if (err)
    {
      connector_destroy (connector);
    }
  return err;
}

gboolean
connector_check (struct connector *connector)
{
  return (connector->inputp && connector->outputp);
}

static struct connector_device *
connector_get_elektron_device (snd_ctl_t * ctl, int card, int device)
{
  snd_rawmidi_info_t *info;
  const gchar *name;
  const gchar *sub_name;
  int subs, subs_in, subs_out;
  int sub;
  int err;
  struct connector_device *connector_device;

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
  if (strncmp (sub_name, "Elektron", 8) == 0)
    {
      debug_print (1, "Adding hw:%d (%s) %s...\n", card, name, sub_name);
      connector_device = malloc (sizeof (struct connector_device));
      connector_device->card = card;
      connector_device->name = strdup (sub_name);
      return connector_device;
    }
  else
    {
      return NULL;
    }
}

static void
connector_fill_card_elektron_devices (gint card, GArray * devices)
{
  snd_ctl_t *ctl;
  gchar name[32];
  gint device;
  gint err;
  struct connector_device *connector_device;

  sprintf (name, "hw:%d", card);
  if ((err = snd_ctl_open (&ctl, name, 0)) < 0)
    {
      error_print ("Cannot open control for card %d: %s\n",
		   card, snd_strerror (err));
      return;
    }
  device = -1;
  while (((err = snd_ctl_rawmidi_next_device (ctl, &device)) == 0)
	 && (device >= 0))
    {
      connector_device = connector_get_elektron_device (ctl, card, device);
      if (connector_device)
	{
	  g_array_append_vals (devices, connector_device, 1);
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
connector_get_elektron_devices ()
{
  gint card, err;
  GArray *devices =
    g_array_new (FALSE, FALSE, sizeof (struct connector_device));

  card = -1;
  while (((err = snd_card_next (&card)) == 0) && (card >= 0))
    {
      connector_fill_card_elektron_devices (card, devices);
    }
  if (err < 0)
    {
      error_print ("Cannot determine card number: %s\n", snd_strerror (err));
    }

  return devices;
}
