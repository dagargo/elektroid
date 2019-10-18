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
#include "connector.h"
#include "utils.h"
#include <byteswap.h>

#define BUFF_SIZE 512
#define TRANSF_BLOCK_SIZE 0x2000

static const guint8 MSG_HEADER[] = { 0xf0, 0, 0x20, 0x3c, 0x10, 0 };

static const guint8 INQ_DEVICE[] = { 0x1 };
static const guint8 INQ_VERSION[] = { 0x2 };
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

static gint
connector_get_msg_status (const GByteArray * msg)
{
  return msg->data[5];
}

static gushort
get_message_seq (GByteArray * msg, gint offset)
{
  uint16_t *seq = (uint16_t *) & msg->data[offset];
  return ntohs (*seq);
}

static gushort
get_rx_message_seq (GByteArray * rx_msg)
{
  return get_message_seq (rx_msg, 2);
}

static gushort
get_tx_message_seq (GByteArray * tx_msg)
{
  return get_message_seq (tx_msg, 0);
}

static void
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
      dir_iterator->dentry =
	(gchar *) & dir_iterator->msg->data[dir_iterator->pos];

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
  GByteArray *payload = g_byte_array_new ();
  guint len = msg->len - sizeof (MSG_HEADER) - 1;

  g_byte_array_append (payload, &msg->data[sizeof (MSG_HEADER)], len);
  transformed = connector_sysex_to_msg (payload);
  free_msg (payload);

  return transformed;
}

static ssize_t
connector_tx_raw (struct connector *connector, const guint8 * data, guint len)
{
  ssize_t tx_len;

  if (!connector->outputp)
    {
      fprintf (stderr, __FILE__ ": Output port is NULL.\n");
      return -1;
    }

  if ((tx_len = snd_rawmidi_write (connector->outputp, data, len)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while sending message.\n");
      connector_destroy (connector);
      return tx_len;
    }
  return tx_len;
}

static ssize_t
connector_tx (struct connector *connector, const GByteArray * msg)
{
  ssize_t ret;
  ssize_t tx_len;
  uint16_t aux;
  guint total;
  guint len;
  guchar *data;
  GByteArray *sysex;
  GByteArray *full_msg;

  g_mutex_lock (&connector->mutex);

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

  full_msg = g_byte_array_new ();
  g_byte_array_append (full_msg, MSG_HEADER, sizeof (MSG_HEADER));
  sysex = connector_msg_to_sysex (msg);
  g_byte_array_append (full_msg, sysex->data, sysex->len);
  free_msg (sysex);
  g_byte_array_append (full_msg, (guint8 *) "\xf7", 1);

  ret = full_msg->len;

  data = full_msg->data;
  total = 0;
  while (total < full_msg->len)
    {
      len =
	full_msg->len - total > BUFF_SIZE ? BUFF_SIZE : full_msg->len - total;
      if ((tx_len = connector_tx_raw (connector, data, len)) < 0)
	{
	  ret = tx_len;
	  goto cleanup;
	}
      data += len;
      total += len;
    }

  debug_print ("Message sent: ");
  debug_print_hex_msg (msg);

cleanup:
  free_msg (full_msg);
  g_mutex_unlock (&connector->mutex);
  return ret;
}

static ssize_t
connector_rx_raw (struct connector *connector, guint8 * data, guint len)
{
  ssize_t rx_len;

  if (!connector->inputp)
    {
      fprintf (stderr, __FILE__ ": Input port is NULL.\n");
      return -1;
    }

  if ((rx_len = snd_rawmidi_read (connector->inputp, data, len)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while receiving message.\n");
      connector_destroy (connector);
      return rx_len;
    }

  return rx_len;
}

static GByteArray *
connector_rx (struct connector *connector)
{
  ssize_t rx_len;
  GByteArray *msg;
  guint8 buffer;
  GByteArray *sysex = g_byte_array_new ();

  //TODO: Skip everything until a SysEx start is found and is from the expected device (start with the same 6 bytes)
  do
    {
      if ((rx_len = connector_rx_raw (connector, &buffer, 1)) < 0)
	{
	  msg = NULL;
	  goto cleanup;
	}
    }
  while (rx_len == 0 || (rx_len == 1 && buffer != 0xf0));

  g_byte_array_append (sysex, &buffer, rx_len);

  do
    {
      if ((rx_len = connector_rx_raw (connector, &buffer, 1)) < 0)
	{
	  msg = NULL;
	  goto cleanup;
	}
      g_byte_array_append (sysex, &buffer, rx_len);
    }
  while (rx_len == 0 || (rx_len > 0 && buffer != 0xf7));

  msg = connector_get_msg_payload (sysex);
  debug_print ("Message received: ");
  debug_print_hex_msg (msg);

cleanup:
  free_msg (sysex);
  return msg;
}

static GByteArray *
connector_tx_and_rx (struct connector *connector, GByteArray * tx_msg)
{
  ssize_t len;
  GByteArray *rx_msg;

  len = connector_tx (connector, tx_msg);
  if (len < 0)
    {
      rx_msg = NULL;
      goto cleanup;
    }
  rx_msg = connector_rx (connector);

cleanup:
  free_msg (tx_msg);
  return rx_msg;
}

static gint
compare_seq (gconstpointer rx_msg_, gconstpointer seq_)
{
  GByteArray *rx_msg = (GByteArray *) rx_msg_;
  gushort seq = *((gushort *) seq_);
  gushort msg_seq = get_rx_message_seq (rx_msg);

  return (seq != msg_seq);
}

static void *
connector_reader (void *data)
{
  GByteArray *rx_msg;
  int err;
  unsigned short revents;
  struct connector *connector = (struct connector *) data;
  int npfds = snd_rawmidi_poll_descriptors_count (connector->inputp);
  struct pollfd *pfds = alloca (npfds * sizeof (struct pollfd));

  snd_rawmidi_poll_descriptors (connector->inputp, pfds, npfds);

  while (connector->inputp)
    {
      err = poll (pfds, npfds, 200);
      if (err < 0)
	{
	  if (errno != EINTR)
	    {
	      fprintf (stderr, __FILE__ ": Poll failed: %s",
		       strerror (errno));
	    }
	  break;
	}
      if (!connector->inputp)
	{
	  break;
	}
      if (snd_rawmidi_poll_descriptors_revents
	  (connector->inputp, pfds, npfds, &revents) < 0)
	{
	  fprintf (stderr, __FILE__ ": Cannot get poll events: %s",
		   snd_strerror (errno));
	  break;
	}
      if (revents & (POLLERR | POLLHUP))
	{
	  break;
	}
      if (!(revents & POLLIN))
	{
	  continue;
	}

      rx_msg = connector_rx (connector);
      if (rx_msg)
	{
	  g_mutex_lock (&connector->mutex);
	  debug_print ("Queuing incoming message...\n");
	  connector->queue = g_slist_append (connector->queue, rx_msg);
	  g_cond_signal (&connector->cond);
	  g_mutex_unlock (&connector->mutex);
	}
    }

  debug_print ("Quitting connector reader...\n");

  return NULL;
}

static GByteArray *
connector_get_response (struct connector *connector, GByteArray * tx_msg)
{
  GByteArray *rx_msg;
  GSList *item;
  gushort seq = get_tx_message_seq (tx_msg);

  debug_print ("Getting response for seq %d...\n", seq);

  g_mutex_lock (&connector->mutex);
  debug_print ("Locking...\n");
  while (!(item = g_slist_find_custom (connector->queue, &seq, compare_seq)))
    {
      g_cond_wait (&connector->cond, &connector->mutex);
    }
  rx_msg = (GByteArray *) item->data;
  connector->queue = g_slist_remove_link (connector->queue, item);
  g_mutex_unlock (&connector->mutex);

  debug_print ("Message found: ");
  debug_print_hex_msg (rx_msg);

  return rx_msg;
}

static GByteArray *
connector_send_and_receive (struct connector *connector, GByteArray * tx_msg)
{
  ssize_t len;
  GByteArray *rx_msg;

  len = connector_tx (connector, tx_msg);
  if (len < 0)
    {
      rx_msg = NULL;
      goto cleanup;
    }
  rx_msg = connector_get_response (connector, tx_msg);

cleanup:
  free_msg (tx_msg);
  return rx_msg;
}

struct connector_dir_iterator *
connector_read_dir (struct connector *connector, gchar * dir)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;

  tx_msg = connector_new_msg_dir_list (dir);
  rx_msg = connector->send_and_receive (connector, tx_msg);
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

  g_byte_array_append (tx_msg, (guchar *) old, strlen (old));
  g_byte_array_append (tx_msg, (guchar *) "\0", 1);
  g_byte_array_append (tx_msg, (guchar *) new, strlen (new));
  g_byte_array_append (tx_msg, (guchar *) "\0", 1);

  rx_msg = connector->send_and_receive (connector, tx_msg);
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
      fprintf (stderr, "%s\n", g_strerror (errno));
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
  GByteArray *tx_msg = connector_new_msg_path (template, size, path);

  rx_msg = connector->send_and_receive (connector, tx_msg);
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
      fprintf (stderr, "%s\n", g_strerror (errno));
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

  tx_msg = connector_new_msg_new_upload (path, fsize);
  rx_msg = connector->send_and_receive (connector, tx_msg);
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
      fprintf (stderr, "%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return id;
}

ssize_t
connector_upload (struct connector *connector, GArray * sample,
		  gchar * path, gint * running, void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t transferred;
  gshort *data;
  gint id;
  int i;

  //TODO: check if the file already exists? (Device makes no difference between creating a new file and creating an already existent file. The new file would be deleted if an upload is not sent, though.)

  id = connector_create_upload (connector, path, sample->len);
  if (id < 0)
    {
      return -1;
    }

  data = (gshort *) sample->data;
  transferred = 0;
  i = 0;
  while (transferred < sample->len && (!running || *running))
    {
      if (progress)
	{
	  progress (transferred / (double) sample->len);
	}

      tx_msg =
	connector_new_msg_upl_blck (id, &data, sample->len, &transferred, i);
      rx_msg = connector->send_and_receive (connector, tx_msg);
      if (!rx_msg)
	{
	  return -1;
	}
      //Response: x, x, x, x, 0xc2, [0 (error), 1 (success)]...
      if (!connector_get_msg_status (rx_msg))
	{
	  fprintf (stderr, "Unexpected status\n");
	}
      free_msg (rx_msg);
      i++;
    }

  if (progress)
    {
      progress (transferred / (double) sample->len);
    }

  debug_print ("%lu frames sent\n", transferred);

  if (!running || *running)
    {
      tx_msg = connector_new_msg_upl_end (id, transferred);
      rx_msg = connector->send_and_receive (connector, tx_msg);
      if (!rx_msg)
	{
	  return -1;
	}
      //Response: x, x, x, x, 0xc1, [0 (error), 1 (success)]...
      if (!connector_get_msg_status (rx_msg))
	{
	  fprintf (stderr, "Unexpected status\n");
	}
      free_msg (rx_msg);
    }

  return transferred;
}

GArray *
connector_download (struct connector *connector, const gchar * path,
		    gint * running, void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *data;
  GArray *result;
  gint id;
  guint frames;
  guint next_block_start;
  guint req_size;
  int offset;
  int16_t v;
  int16_t *frame;
  int i;

  tx_msg = connector_new_msg_info_file (path);
  rx_msg = connector->send_and_receive (connector, tx_msg);
  if (!rx_msg)
    {
      return NULL;
    }
  connector_get_sample_info_from_msg (rx_msg, &id, &frames);
  free_msg (rx_msg);
  if (id < 0)
    {
      fprintf (stderr, "File %s not found\n", path);
      result = NULL;
      goto end;
    }

  debug_print ("frames %d\n", frames);

  data = g_byte_array_new ();

  next_block_start = 0;
  offset = 64;
  while (next_block_start < frames && (!running || *running))
    {
      if (progress)
	{
	  progress (next_block_start / (double) frames);
	}

      req_size =
	frames - next_block_start >
	TRANSF_BLOCK_SIZE ? TRANSF_BLOCK_SIZE : frames - next_block_start;
      tx_msg = connector_new_msg_dwnl_blck (id, next_block_start, req_size);
      rx_msg = connector->send_and_receive (connector, tx_msg);
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
    }

  if (progress)
    {
      progress (next_block_start / (double) frames);
    }

  debug_print ("%d bytes received\n", next_block_start);

  result = g_array_new (FALSE, FALSE, sizeof (short));

  frame = (gshort *) data->data;
  for (i = 0; i < data->len; i += 2)
    {
      v = ntohs (*frame);
      g_array_append_val (result, v);
      frame++;
    }

  tx_msg = connector_new_msg_end_download (id);
  rx_msg = connector->send_and_receive (connector, tx_msg);
  if (!rx_msg)
    {
      result = NULL;
      goto cleanup;
    }
  //Response: x, x, x, x, 0xb1, [0 (error), 1 (success)]...
  if (!connector_get_msg_status (rx_msg))
    {
      fprintf (stderr, "Unexpected status\n");
    }
  free_msg (rx_msg);

cleanup:
  free_msg (data);
end:
  return result;
}

gint
connector_create_dir (struct connector *connector, const gchar * path)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint res;

  tx_msg = connector_new_msg_new_dir (path);
  rx_msg = connector->send_and_receive (connector, tx_msg);
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
      fprintf (stderr, "%s\n", g_strerror (errno));
    }
  free_msg (rx_msg);

  return res;
}

void
connector_destroy (struct connector *connector)
{
  int err;

  debug_print ("Destroying connector...\n");

  snd_rawmidi_drain (connector->inputp);
  if (connector->inputp)
    {
      err = snd_rawmidi_close (connector->inputp);
      if (err)
	{
	  fprintf (stderr, __FILE__ ": Error while closing MIDI port: %s\n",
		   g_strerror (errno));
	}
      connector->inputp = NULL;
    }

  snd_rawmidi_drain (connector->outputp);
  if (connector->outputp)
    {
      err = snd_rawmidi_close (connector->outputp);
      if (err)
	{
	  fprintf (stderr, __FILE__ ": Error while closing MIDI port: %s\n",
		   g_strerror (errno));
	}
      connector->outputp = NULL;
    }

  if (connector->reader_thread)
    {
      g_thread_join (connector->reader_thread);
      g_thread_unref (connector->reader_thread);
    }
  connector->reader_thread = NULL;

  free (connector->device_name);

  g_slist_free_full (connector->queue, free_msg);
  connector->queue = NULL;
}

gint
connector_init (struct connector *connector, gint card,
		enum connector_mode mode)
{
  int err;
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *rx_msg_fw_ver;
  gchar name[32];
  sprintf (name, "hw:%d", card);

  connector->inputp = NULL;
  connector->outputp = NULL;
  connector->device_name = NULL;

  connector->queue = NULL;
  connector->reader_thread = NULL;

  if (card < 0)
    {
      debug_print ("Invalid card.\n");
      err = -1;
      goto cleanup;
    }

  debug_print ("Initializing connector to '%s'...\n", name);

  if ((err =
       snd_rawmidi_open (&connector->inputp, &connector->outputp,
			 name, SND_RAWMIDI_NONBLOCK)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while opening MIDI port.\n");
      goto cleanup;
    }

  debug_print ("Setting blocking mode...\n");
  if ((err = snd_rawmidi_nonblock (connector->outputp, 0)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while setting blocking mode\n");
      goto cleanup;
    }
  if ((err = snd_rawmidi_nonblock (connector->inputp, 0)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while setting blocking mode\n");
      goto cleanup;
    }

  debug_print ("Stopping device...\n");
  if (snd_rawmidi_write (connector->outputp, "\xfc", 1) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while stopping device\n");
    }

  connector->seq = 0;
  connector->device_name = malloc (LABEL_MAX);

  tx_msg = connector_new_msg_data (INQ_DEVICE, sizeof (INQ_DEVICE));
  rx_msg = connector_tx_and_rx (connector, tx_msg);

  tx_msg = connector_new_msg_data (INQ_VERSION, sizeof (INQ_VERSION));
  rx_msg_fw_ver = connector_tx_and_rx (connector, tx_msg);

  snprintf (connector->device_name, LABEL_MAX, "%s %s", &rx_msg->data[23],
	    &rx_msg_fw_ver->data[10]);
  free_msg (rx_msg);
  free_msg (rx_msg_fw_ver);
  debug_print ("Connected to %s\n", connector->device_name);

  if (mode == SINGLE_THREAD)
    {
      connector->send_and_receive = connector_tx_and_rx;
    }
  else if (mode == MULTI_THREAD)
    {
      connector->send_and_receive = connector_send_and_receive;
      connector->reader_thread =
	g_thread_new ("connector_reader", connector_reader, connector);
    }

  return err;

cleanup:
  connector_destroy (connector);
  return err;
}

int
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
      fprintf (stderr,
	       __FILE__ ": cannot get rawmidi information %d:%d:%d: %s\n",
	       card, device, sub, snd_strerror (err));
      return NULL;
    }

  name = snd_rawmidi_info_get_name (info);
  sub_name = snd_rawmidi_info_get_subdevice_name (info);
  if (strncmp (sub_name, "Elektron", 8) == 0)
    {
      debug_print ("Adding hw:%d (%s) %s...\n", card, name, sub_name);
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
      fprintf (stderr, __FILE__ ": cannot open control for card %d: %s", card,
	       snd_strerror (err));
      return;
    }
  device = -1;
  while (((err = snd_ctl_rawmidi_next_device (ctl, &device)) == 0)
	 && (device >= 0))
    {
      connector_device = connector_get_elektron_device (ctl, card, device);
      if (card >= 0)
	{
	  g_array_append_vals (devices, connector_device, 1);
	}
    }
  if (err < 0)
    {
      fprintf (stderr, __FILE__ ": cannot determine card number: %s",
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
      fprintf (stderr, __FILE__ ": cannot determine card number: %s",
	       snd_strerror (err));
    }

  return devices;
}
