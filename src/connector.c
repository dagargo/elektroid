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
static const guint8 INQ_INFO_FILE_TEMPLATE[] = { 0x30 };
static const guint8 INQ_NEW_DIR_TEMPLATE[] = { 0x11 };
static const guint8 INQ_DWL_TEMPLATE[] =
  { 0x32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
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

struct connector_dir_iterator *
connector_new_dir_iterator (const GByteArray * dir_payload)
{
  struct connector_dir_iterator *dir_iterator =
    malloc (sizeof (struct connector_dir_iterator));

  dir_iterator->dentry = NULL;
  dir_iterator->dir_payload = dir_payload;
  dir_iterator->position = 5;

  return dir_iterator;
}

guint
connector_get_next_dentry (struct connector_dir_iterator *dir_iterator)
{
  dir_iterator->position += 9;

  if (dir_iterator->position >= dir_iterator->dir_payload->len)
    {
      dir_iterator->dentry = NULL;
      return -ENOENT;
    }
  else
    {
      dir_iterator->type =
	dir_iterator->dir_payload->data[dir_iterator->position];
      dir_iterator->position++;
      dir_iterator->dentry = (gchar *)
	& dir_iterator->dir_payload->data[dir_iterator->position];
      while (dir_iterator->position < dir_iterator->dir_payload->len
	     && dir_iterator->dir_payload->data[dir_iterator->position] != 0)
	dir_iterator->position++;
      dir_iterator->position++;
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
connector_get_sample_info_from_msg (GByteArray * info_msg, guint * id,
				    guint * size)
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

static GByteArray *
connector_new_msg_data (const guint8 * data, guint len)
{
  GByteArray *msg = g_byte_array_new ();

  g_byte_array_append (msg, (guchar *) "\0\0\0\0", 4);
  g_byte_array_append (msg, data, len);

  return msg;
}

static GByteArray *
connector_new_x_payload (const guint8 * data, guint len, const gchar * path)
{
  GByteArray *msg = connector_new_msg_data (data, len);

  g_byte_array_append (msg, (guchar *) path, strlen (path));
  g_byte_array_append (msg, (guchar *) "\0", 1);

  return msg;
}

GByteArray *
connector_new_msg_dir_list (const gchar * path)
{
  return connector_new_x_payload (INQ_LS_DIR_TEMPLATE,
				  sizeof (INQ_LS_DIR_TEMPLATE), path);
}

GByteArray *
connector_new_msg_info_file (const gchar * path)
{
  return connector_new_x_payload (INQ_INFO_FILE_TEMPLATE,
				  sizeof (INQ_INFO_FILE_TEMPLATE), path);
}

GByteArray *
connector_new_msg_new_dir (const gchar * path)
{
  return connector_new_x_payload (INQ_NEW_DIR_TEMPLATE,
				  sizeof (INQ_NEW_DIR_TEMPLATE), path);
}

GByteArray *
connector_new_msg_new_upload (const gchar * path, guint frames)
{
  uint32_t aux32;
  GByteArray *msg = connector_new_x_payload (INQ_NEW_TEMPLATE,
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
  g_byte_array_free (payload, TRUE);

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

ssize_t
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

  aux = htons (connector->seq);
  memcpy (msg->data, &aux, sizeof (uint16_t));
  connector->seq++;

  full_msg = g_byte_array_new ();
  g_byte_array_append (full_msg, MSG_HEADER, sizeof (MSG_HEADER));
  sysex = connector_msg_to_sysex (msg);
  g_byte_array_append (full_msg, sysex->data, sysex->len);
  g_byte_array_free (sysex, TRUE);
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
  g_byte_array_free (full_msg, TRUE);
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

GByteArray *
connector_rx (struct connector *connector)
{
  ssize_t rx_len;
  GByteArray *msg;
  guint8 *buffer = malloc (BUFF_SIZE);
  GByteArray *sysex = g_byte_array_new ();

  //TODO: Skip everything until a SysEx start is found and is from the expected device (start with the same 6 bytes)
  do
    {
      if ((rx_len = connector_rx_raw (connector, buffer, 1)) < 0)
	{
	  msg = NULL;
	  goto cleanup;
	}
    }
  while (rx_len == 0 || (rx_len == 1 && buffer[0] != 0xf0));

  g_byte_array_append (sysex, buffer, 1);

  do
    {
      if ((rx_len = connector_rx_raw (connector, buffer, BUFF_SIZE)) < 0)
	{
	  msg = NULL;
	  goto cleanup;
	}
      g_byte_array_append (sysex, buffer, rx_len);
    }
  while (rx_len == 0 || (rx_len > 0 && buffer[rx_len - 1] != 0xf7));

  msg = connector_get_msg_payload (sysex);
  debug_print ("Message received: ");
  debug_print_hex_msg (msg);

cleanup:
  free (buffer);
  g_byte_array_free (sysex, TRUE);
  return msg;
}

ssize_t
connector_upload (struct connector *connector, GArray * sample,
		  guint id, int *running, void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t transferred;
  gshort *data;
  int i;
  ssize_t sent;

  data = (gshort *) sample->data;
  transferred = 0;
  i = 0;
  while (transferred < sample->len && *running)
    {
      if (progress)
	{
	  progress (transferred / (double) sample->len);
	}

      tx_msg =
	connector_new_msg_upl_blck (id, &data, sample->len, &transferred, i);
      sent = connector_tx (connector, tx_msg);
      g_byte_array_free (tx_msg, TRUE);
      if (sent < 0)
	{
	  return -1;
	}

      rx_msg = connector_rx (connector);
      if (!rx_msg)
	{
	  return -1;
	}
      //TODO: check message
      g_byte_array_free (rx_msg, TRUE);
      i++;
    }

  if (progress)
    {
      progress (transferred / (double) sample->len);
    }

  debug_print ("%lu frames sent\n", transferred);

  if (*running)
    {
      tx_msg = connector_new_msg_upl_end (id, transferred);
      sent = connector_tx (connector, tx_msg);
      g_byte_array_free (tx_msg, TRUE);
      if (sent < 0)
	{
	  return sent;
	}

      rx_msg = connector_rx (connector);
      if (!rx_msg)
	{
	  return -1;
	}
      g_byte_array_free (rx_msg, TRUE);
    }

  return transferred;
}

GArray *
connector_download (struct connector *connector, const char *path,
		    int *running, void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *data;
  GArray *result;
  guint len;
  guint id;
  guint next_block_start;
  guint req_size;
  int offset;
  ssize_t sent;
  int16_t v;
  gshort *frames;
  int i;

  tx_msg = connector_new_msg_info_file (path);
  sent = connector_tx (connector, tx_msg);
  g_byte_array_free (tx_msg, TRUE);
  if (sent < 0)
    {
      return NULL;
    }

  rx_msg = connector_rx (connector);
  if (!rx_msg)
    {
      return NULL;
    }
  connector_get_sample_info_from_msg (rx_msg, &id, &len);
  g_byte_array_free (rx_msg, TRUE);

  debug_print ("len %d\n", len);

  data = g_byte_array_new ();

  next_block_start = 0;
  offset = 64;
  while (next_block_start < len && *running)
    {
      if (progress)
	{
	  progress (next_block_start / (double) len);
	}

      req_size =
	len - next_block_start >
	TRANSF_BLOCK_SIZE ? TRANSF_BLOCK_SIZE : len - next_block_start;
      tx_msg = connector_new_msg_dwnl_blck (id, next_block_start, req_size);
      sent = connector_tx (connector, tx_msg);
      g_byte_array_free (tx_msg, TRUE);
      if (sent < 0)
	{
	  result = NULL;
	  goto cleanup;
	}

      rx_msg = connector_rx (connector);
      if (!rx_msg)
	{
	  result = NULL;
	  goto cleanup;
	}
      g_byte_array_append (data, &rx_msg->data[22 + offset],
			   req_size - offset);
      g_byte_array_free (rx_msg, TRUE);

      next_block_start += req_size;
      offset = 0;
    }

  if (progress)
    {
      progress (next_block_start / (double) len);
    }

  debug_print ("%d bytes received\n", next_block_start);

  result = g_array_new (FALSE, FALSE, sizeof (short));

  frames = (gshort *) data->data;
  for (i = 0; i < data->len; i += 2)
    {
      v = __bswap_16 (*frames);
      g_array_append_val (result, v);
      frames++;
    }

cleanup:
  g_byte_array_free (data, TRUE);
  return result;
}

gint
connector_create_upload (struct connector *connector, char *path, guint fsize)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t len;
  guint id;

  tx_msg = connector_new_msg_new_upload (path, fsize);
  len = connector_tx (connector, tx_msg);
  g_byte_array_free (tx_msg, TRUE);
  if (len < 0)
    {
      return -1;
    }

  rx_msg = connector_rx (connector);
  if (!rx_msg)
    {
      return -1;
    }
  //Response is always ok: x, x, x, x, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x04
  connector_get_sample_info_from_msg (rx_msg, &id, NULL);
  g_byte_array_free (rx_msg, TRUE);

  return id;
}

gint
connector_create_dir (struct connector *connector, char *path)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t len;

  tx_msg = connector_new_msg_new_dir (path);
  len = connector_tx (connector, tx_msg);
  g_byte_array_free (tx_msg, TRUE);
  if (len < 0)
    {
      return -1;
    }

  rx_msg = connector_rx (connector);
  if (!rx_msg)
    {
      return -1;
    }
  //Response is always ok: x, x, x, x, 0x91, 0x01
  g_byte_array_free (rx_msg, TRUE);

  return 0;
}

void
connector_destroy (struct connector *connector)
{
  debug_print ("Destroying connector...\n");

  if (connector->inputp)
    {
      snd_rawmidi_close (connector->inputp);
      connector->inputp = NULL;
    }

  if (connector->outputp)
    {
      snd_rawmidi_close (connector->outputp);
      connector->outputp = NULL;
    }

  if (connector->device_name)
    {
      free (connector->device_name);
    }
}

int
connector_init (struct connector *connector, gint card)
{
  int err;
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *rx_msg_fw_ver;
  char name[32];
  sprintf (name, "hw:%d", card);

  connector->inputp = NULL;
  connector->outputp = NULL;
  connector->device_name = NULL;

  if (card < 0)
    {
      debug_print ("Invalid card.\n");
      return -1;
    }

  debug_print ("Initializing connector to '%s'...\n", name);

  if ((err =
       snd_rawmidi_open (&connector->inputp, &connector->outputp,
			 name, SND_RAWMIDI_NONBLOCK)) < 0)
    {
      fprintf (stderr, "Error while opening MIDI port.\n");
      return err;
    }

  debug_print ("Setting blocking mode...\n");
  if ((err = snd_rawmidi_nonblock (connector->outputp, 0)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while setting blocking mode.\n");
      goto cleanup;
    }
  if ((err = snd_rawmidi_nonblock (connector->inputp, 0)) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while setting blocking mode.\n");
      goto cleanup;
    }

  debug_print ("Stopping device...\n");
  if (snd_rawmidi_write (connector->outputp, "\xfc", 1) < 0)
    {
      fprintf (stderr, __FILE__ ": Error while stopping device.\n");
    }

  connector->seq = 0;
  connector->device_name = malloc (LABEL_MAX);

  tx_msg = connector_new_msg_data (INQ_DEVICE, sizeof (INQ_DEVICE));
  connector_tx (connector, tx_msg);
  g_byte_array_free (tx_msg, TRUE);
  rx_msg = connector_rx (connector);

  tx_msg = connector_new_msg_data (INQ_VERSION, sizeof (INQ_VERSION));
  connector_tx (connector, tx_msg);
  g_byte_array_free (tx_msg, TRUE);
  rx_msg_fw_ver = connector_rx (connector);

  snprintf (connector->device_name, LABEL_MAX, "%s %s", &rx_msg->data[23],
	    &rx_msg_fw_ver->data[10]);
  g_byte_array_free (rx_msg, TRUE);
  g_byte_array_free (rx_msg_fw_ver, TRUE);
  debug_print ("Connected to %s\n", connector->device_name);

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
  const char *name;
  const char *sub_name;
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
      debug_print ("Adding hw:%d (%s) %s...\n", sub, name, sub_name);
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
connector_fill_card_devices (int card, GArray * devices)
{
  snd_ctl_t *ctl;
  char name[32];
  int device;
  int err;
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
  int card, err;
  GArray *devices =
    g_array_new (FALSE, FALSE, sizeof (struct connector_device));

  card = -1;
  while (((err = snd_card_next (&card)) == 0) && (card >= 0))
    {
      connector_fill_card_devices (card, devices);
    }
  if (err < 0)
    {
      fprintf (stderr, __FILE__ ": cannot determine card number: %s",
	       snd_strerror (err));
    }

  return devices;
}
