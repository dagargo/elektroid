/*
 *   sds.c
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

#include <math.h>
#include <string.h>
#include <glib/gi18n.h>
#include "elektron.h"
#include "sds.h"
#include "default.h"
#include "common.h"

#define SDS_SAMPLE_LIMIT 1000
#define SDS_DATA_PACKET_LEN 127
#define SDS_DATA_PACKET_PAYLOAD_LEN 120
#define SDS_DATA_PACKET_CKSUM_POS 125
#define SDS_DATA_PACKET_CKSUM_START 1
#define SDS_BYTES_PER_WORD 3
#define SDS_MAX_RETRIES 5
#define SDS_SPEC_TIMEOUT 20	//Timeout in the specs to consider no response when transmission is going on.
#define SDS_SPEC_TIMEOUT_HANDSHAKE 2000	//Timeout in the specs to consider no response during the handshake.
#define SDS_NO_SPEC_TIMEOUT 5000	//Timeout used when the specs indicate to wait indefinitely.
#define SDS_NO_SPEC_TIMEOUT_TRY 1500	//Timeout for SDS extensions that might not be implemented.
#define SDS_REST_TIME_DEFAULT 50000	//Rest time to not overwhelm the devices when sending consecutive packets. Lower values cause an an E-Mu ESI-2000 to send corrupted packets.s
#define SDS_INCOMPLETE_PACKET_TIMEOUT 2000
#define SDS_NO_SPEC_OPEN_LOOP_REST_TIME 200000
#define SDS_SAMPLE_CHANNELS 1
#define SDS_SAMPLE_NAME_MAX_LEN 127

struct sds_data
{
  gint rest_time;
  gboolean name_extension;
};

struct sds_iterator_data
{
  guint32 next;
  struct backend *backend;
};

static const guint8 SDS_SAMPLE_REQUEST[] = { 0xf0, 0x7e, 0, 0x3, 0, 0, 0xf7 };
static const guint8 SDS_ACK[] = { 0xf0, 0x7e, 0, 0x7f, 0, 0xf7 };
static const guint8 SDS_NAK[] = { 0xf0, 0x7e, 0, 0x7e, 0, 0xf7 };
static const guint8 SDS_CANCEL[] = { 0xf0, 0x7e, 0, 0x7d, 0, 0xf7 };
static const guint8 SDS_WAIT[] = { 0xf0, 0x7e, 0, 0x7c, 0, 0xf7 };
static const guint8 SDS_SAMPLE_NAME_REQUEST[] =
  { 0xf0, 0x7e, 0, 0x5, 0x4, 0, 0, 0xf7 };
static const guint8 SDS_DATA_PACKET_HEADER[] = { 0xf0, 0x7e, 0, 0x2, 0 };
static const guint8 SDS_SAMPLE_NAME_HEADER[] =
  { 0xf0, 0x7e, 0, 0x5, 0x3, 0, 0, 0 };
static const guint8 SDS_DUMP_HEADER[] =
  { 0xf0, 0x7e, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };
static const guint8 SDS_LOOP_POINT_REQUEST[] =
  { 0xf0, 0x7e, 0, 5, 2, 0, 0, 0, 0, 0xf7 };

static gchar *
sds_get_sample_name (struct backend *backend, gint index)
{
  GByteArray *tx_msg, *rx_msg;
  gchar *name = NULL;

  tx_msg = g_byte_array_new ();
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_REQUEST,
		       sizeof (SDS_SAMPLE_NAME_REQUEST));
  tx_msg->data[5] = index % 0x80;
  tx_msg->data[6] = index / 0x80;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SDS_NO_SPEC_TIMEOUT);
  if (rx_msg)
    {
      size_t n = rx_msg->data[9];
      name = g_malloc (sizeof (gchar) * (SDS_SAMPLE_NAME_MAX_LEN + 1));
      memcpy (name, (gchar *) & rx_msg->data[10], n);
      memset (name + n, 0, SDS_SAMPLE_NAME_MAX_LEN + 1 - n);
      free_msg (rx_msg);
    }

  return name;
}

static gchar *
sds_get_download_path (struct backend *backend,
		       const struct fs_operations *ops, const gchar *dst_dir,
		       const gchar *src_path, struct idata *sample)
{
  GString *str = g_string_new (NULL);
  gchar *path;
  gchar *name = g_path_get_basename (src_path);
  gint index = atoi (name);
  gboolean use_id = TRUE;
  struct sds_data *sds_data = backend->data;

  if (sds_data->name_extension)
    {
      gchar *sample_name = sds_get_sample_name (backend, index);
      if (sample_name)
	{
	  g_string_append_printf (str, "%s.wav", sample_name);
	  g_free (name);
	  use_id = FALSE;
	}
    }

  if (use_id)
    {
      g_string_append_printf (str, "%03d.wav", index);
    }

  path = path_chain (PATH_SYSTEM, dst_dir, str->str);
  g_string_free (str, TRUE);
  return path;
}

static guint
sds_get_bytes_value_right_just (guint8 *data, gint length)
{
  gint value = 0;
  for (gint i = 0, shift = 0; i < length; i++, shift += 7)
    {
      value |= data[i] << shift;
    }
  return value;
}

static void
sds_set_bytes_value_right_just (guint8 *data, gint length, guint value)
{
  for (gint i = 0, shift = 0; i < length; i++, shift += 7)
    {
      *data = 0x7f & (value >> shift);
      data++;
    }
}

static gint16
sds_get_gint16_value_left_just (guint8 *data, gint length, guint bits)
{
  guint value = 0;
  gint16 svalue;
  for (gint i = length - 1, shift = 0; i >= 0; i--, shift += 7)
    {
      value |= (((guint) data[i]) << shift);
    }
  value >>= length * 7 - bits;
  svalue = (gint16) (value - 0x8000);
  return svalue;
}

static void
sds_set_gint16_value_left_just (guint8 *data, gint length, guint bits,
				gint16 svalue)
{
  gint value = svalue;
  value += (guint) 0x8000;
  value <<= length * 7 - bits;
  for (gint i = length - 1, shift = 0; i >= 0; i--, shift += 7)
    {
      data[i] = (guint8) (0x7f & (value >> shift));
    }
}

static guint8
sds_checksum (guint8 *data)
{
  guint8 checksum = 0;
  for (int i = SDS_DATA_PACKET_CKSUM_START; i < SDS_DATA_PACKET_CKSUM_POS;
       i++)
    {
      checksum ^= data[i];
    }
  checksum &= 0x7F;
  return checksum;
}

static gint
sds_get_bytes_per_word (gint32 bits, guint *word_size, guint *bytes_per_word)
{
  *word_size = (guint) ceil (bits / 8.0);
  if (*word_size != 2)
    {
      error_print ("%d bits resolution not supported", bits);
      return -1;
    }

  if (bits < 15)
    {
      *bytes_per_word = 2;
    }
  else
    {
      *bytes_per_word = 3;
    }

  return 0;
}

static gint
sds_tx_handshake (struct backend *backend, const guint8 *msg, guint8 packet)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SDS_ACK));
  g_byte_array_append (tx_msg, msg, sizeof (SDS_ACK));
  tx_msg->data[4] = packet;
  return backend_tx (backend, tx_msg);
}

static guint
sds_get_download_info (GByteArray *header, struct sample_info *sample_info,
		       guint *bits, guint *words, guint *word_size,
		       guint *bytes_per_word)
{
  *bits = header->data[6];
  if (*bits == 8)
    {
      sample_info->format = SF_FORMAT_WAV | SF_FORMAT_PCM_U8;
    }
  else
    {
      sample_info->format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    }
  if (sds_get_bytes_per_word (*bits, word_size, bytes_per_word))
    {
      return -1;
    }
  sample_info->rate = 1.0e9 /
    sds_get_bytes_value_right_just (&header->data[7], SDS_BYTES_PER_WORD);
  *words =
    sds_get_bytes_value_right_just (&header->data[10], SDS_BYTES_PER_WORD);
  sample_info->loop_start =
    sds_get_bytes_value_right_just (&header->data[13], SDS_BYTES_PER_WORD);
  sample_info->loop_end =
    sds_get_bytes_value_right_just (&header->data[16], SDS_BYTES_PER_WORD);
  sample_info->loop_type = header->data[19];
  sample_info->midi_note = 0;
  sample_info->channels = 1;
  return 0;
}

static inline gboolean
sds_check_message_id (GByteArray *msg, guint id)
{
  return (msg->data[4] == id % 0x80 && msg->data[5] == id / 0x80);
}

static inline void
sds_set_message_id (GByteArray *tx_msg, guint id)
{
  tx_msg->data[4] = id % 0x80;
  tx_msg->data[5] = id / 0x80;
}

static GByteArray *
sds_get_request_msg (guint id)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SDS_SAMPLE_REQUEST));
  g_byte_array_append (tx_msg, SDS_SAMPLE_REQUEST,
		       sizeof (SDS_SAMPLE_REQUEST));
  sds_set_message_id (tx_msg, id);
  return tx_msg;
}

static GByteArray *
sds_get_dump_msg (guint id, guint frames, struct sample_info *sample_info,
		  guint bits)
{
  guint period;
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SDS_DUMP_HEADER));
  g_byte_array_append (tx_msg, SDS_DUMP_HEADER, sizeof (SDS_DUMP_HEADER));
  sds_set_message_id (tx_msg, id);

  if (sample_info)
    {
      tx_msg->data[6] = (guint8) bits;
      period = 1.0e9 / sample_info->rate;
      sds_set_bytes_value_right_just (&tx_msg->data[7], SDS_BYTES_PER_WORD,
				      period);
      sds_set_bytes_value_right_just (&tx_msg->data[10], SDS_BYTES_PER_WORD,
				      frames);
      sds_set_bytes_value_right_just (&tx_msg->data[13], SDS_BYTES_PER_WORD,
				      sample_info->loop_start);
      sds_set_bytes_value_right_just (&tx_msg->data[16], SDS_BYTES_PER_WORD,
				      sample_info->loop_end);
      tx_msg->data[19] = (sample_info->loop_start == sample_info->loop_end
			  && sample_info->loop_start ==
			  frames - 1) ? 0x7f : sample_info->loop_type;
    }

  return tx_msg;
}

static GByteArray *
sds_rx (struct backend *backend, gint timeout)
{
  struct sysex_transfer transfer;
  transfer.timeout = timeout;
  transfer.batch = FALSE;
  g_mutex_lock (&backend->mutex);
  backend_rx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  return transfer.raw;
}

static void
sds_download_inc_packet (gboolean *first, guint *packet)
{
  if (*first)
    {
      *first = FALSE;
    }
  else
    {
      (*packet)++;
    }
}

static void
sds_debug_print_sample_data (guint bits, guint bytes_per_word,
			     guint word_size, guint sample_rate, guint words,
			     guint packets)
{
  debug_print (1,
	       "Resolution: %d bits; %d bytes per word; word size %d bytes.",
	       bits, bytes_per_word, word_size);
  debug_print (1, "Sample rate: %d Hz", sample_rate);
  debug_print (1, "Words: %d", words);
  debug_print (1, "Packets: %d", packets);
}

static GByteArray *
sds_download_get_header (struct backend *backend, guint id)
{
  GByteArray *tx_msg, *rx_msg;

  tx_msg = sds_get_request_msg (id);
  g_byte_array_append (tx_msg, SDS_WAIT, sizeof (SDS_WAIT));	//We add a WAIT packet.
  tx_msg->data[11] = 0;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SDS_NO_SPEC_TIMEOUT);

  if (rx_msg && rx_msg->len == sizeof (SDS_DUMP_HEADER)
      && !memcmp (rx_msg->data, SDS_DUMP_HEADER, 4)
      && sds_check_message_id (rx_msg, id))
    {
      return rx_msg;
    }

  debug_print (1, "Bad dump header");

  return NULL;
}

static gint
sds_download_try (struct backend *backend, const gchar *path,
		  struct idata *sample, struct job_control *control)
{
  guint id, words, word_size, read_bytes, bytes_per_word, total_words, err,
    retries, packets, packet, exp_packet, rx_packets, bits;
  gint16 s;
  GByteArray *tx_msg, *rx_msg;
  gchar *name;
  guint8 *dataptr;
  gboolean active, first;
  gboolean last_packet_ack;
  struct sample_info *sample_info;
  struct sysex_transfer transfer;
  struct sds_data *sds_data = backend->data;
  GByteArray *output = g_byte_array_new ();

  name = g_path_get_basename (path);
  id = atoi (name);
  g_free (name);

  debug_print (1, "Sending dump request...");
  packet = 0;

  retries = 0;
  while (1)
    {
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      if (!active)
	{
	  err = -ECANCELED;
	  goto end;
	}

      g_mutex_lock (&backend->mutex);
      backend_rx_drain (backend);
      g_mutex_unlock (&backend->mutex);

      rx_msg = sds_download_get_header (backend, id);
      if (rx_msg)
	{
	  break;
	}
      retries++;
      if (retries == SDS_MAX_RETRIES)
	{
	  err = -EIO;
	  goto end;
	}
    }

  sample_info = g_malloc (sizeof (struct sample_info));
  if (sds_get_download_info (rx_msg, sample_info, &bits, &words, &word_size,
			     &bytes_per_word))
    {
      free_msg (rx_msg);
      g_free (sample_info);
      err = -EINVAL;
      goto end;
    }

  packets =
    ceil (words / (double) (SDS_DATA_PACKET_PAYLOAD_LEN / bytes_per_word));
  sds_debug_print_sample_data (bits, bytes_per_word, word_size,
			       sample_info->rate, words, packets);

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);
  control->parts = 1;
  control->part = 0;
  job_control_set_progress (control, 0.0);

  debug_print (1, "Receiving dump data...");

  tx_msg = g_byte_array_new ();
  total_words = 0;
  retries = 0;
  last_packet_ack = TRUE;
  err = 0;
  exp_packet = 0;
  first = TRUE;
  rx_packets = 0;
  while (active && rx_packets <= packets)
    {
      if (retries == SDS_MAX_RETRIES)
	{
	  debug_print (1, "Too many retries");
	  err = -EIO;
	  break;
	}

      g_byte_array_set_size (tx_msg, 0);
      if (last_packet_ack)
	{
	  g_byte_array_append (tx_msg, SDS_ACK, sizeof (SDS_ACK));
	}
      else
	{
	  g_byte_array_append (tx_msg, SDS_NAK, sizeof (SDS_NAK));
	}
      tx_msg->data[4] = packet % 0x80;

      if (rx_packets == packets)
	{
	  err = backend_tx (backend, tx_msg);
	  goto end;
	}

      if (last_packet_ack)
	{
	  sds_download_inc_packet (&first, &packet);
	}

      g_byte_array_append (tx_msg, SDS_WAIT, sizeof (SDS_WAIT));
      tx_msg->data[10] = (packet) % 0x80;
      transfer.raw = tx_msg;
      transfer.timeout = SDS_INCOMPLETE_PACKET_TIMEOUT;	//This is enough to detect incomplete packets.
      err = backend_tx_and_rx_sysex_transfer (backend, &transfer, FALSE);
      if (err == -ECANCELED)
	{
	  break;
	}
      else if (err == -ETIMEDOUT)
	{
	  debug_print (2,
		       "Packet not received. Remaining packets: %d; remaining samples: %d",
		       packets - rx_packets, words - total_words);
	  //This is a hack to fix a downloading error with an E-Mu ESI-2000 as it never sends the last packet when there is only 1 sample.
	  if ((rx_packets == packets - 1) && (total_words == words - 1))
	    {
	      debug_print (2,
			   "Skipping last packet as it has only one sample...");
	      rx_packets++;

	      //We cancel the upload.
	      usleep (sds_data->rest_time);
	      sds_tx_handshake (backend, SDS_CANCEL, packet % 0x80);
	      usleep (sds_data->rest_time);

	      err = 0;
	      goto end;
	    }
	  rx_msg = NULL;
	  goto retry;
	}
      else
	{
	  rx_msg = transfer.raw;
	}

      if (rx_msg->len != SDS_DATA_PACKET_LEN)
	{
	  debug_print (2, "Invalid length");
	  goto retry;
	}

      guint exp_packet_id = exp_packet % 0x80;
      if (rx_msg->data[4] != exp_packet_id)
	{
	  debug_print (2, "Invalid packet number (%d != %d)",
		       rx_msg->data[4], exp_packet_id);
	  goto retry;
	}

      if (sds_checksum (rx_msg->data) !=
	  rx_msg->data[SDS_DATA_PACKET_CKSUM_POS])
	{
	  debug_print (2, "Invalid cksum");
	  goto retry;
	}

      exp_packet++;
      rx_packets++;

      last_packet_ack = TRUE;
      retries = 0;

      read_bytes = 0;
      dataptr = &rx_msg->data[5];
      while (read_bytes < SDS_DATA_PACKET_PAYLOAD_LEN && total_words < words)
	{
	  s = sds_get_gint16_value_left_just (dataptr, bytes_per_word, bits);
	  g_byte_array_append (output, (guint8 *) & s, sizeof (gint16));
	  dataptr += bytes_per_word;
	  read_bytes += bytes_per_word;
	  total_words++;
	}

      job_control_set_progress (control, rx_packets / (double) packets);

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      free_msg (rx_msg);

      continue;

    retry:
      debug_print (2, "Retrying packet...");
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
      last_packet_ack = FALSE;
      usleep (sds_data->rest_time);
      retries++;
      continue;
    }

  free_msg (tx_msg);

end:
  if (active && !err && rx_packets == packets)
    {
      debug_print (1, "%d frames received", total_words);
      job_control_set_progress (control, 1.0);
      idata_init (sample, output, NULL, sample_info);
    }
  else
    {
      debug_print (1, "Cancelling SDS download...");
      sds_tx_handshake (backend, SDS_CANCEL, packet % 0x80);
      g_byte_array_free (output, TRUE);
    }

  usleep (sds_data->rest_time);

  return err;
}

static gint
sds_download (struct backend *backend, const gchar *path,
	      struct idata *sample, struct job_control *control)
{
  gint err;

  for (gint i = 0; i < SDS_MAX_RETRIES; i++)
    {
      err = sds_download_try (backend, path, sample, control);
      if (err == -EBADMSG)
	{
	  //We retry the whole download to fix a downloading error with an E-Mu ESI-2000 as it occasionally doesn't send the last packet.
	  debug_print (2, "Bug detected. Retrying download...");
	}
      else
	{
	  break;
	}
    }

  return err;
}

static gint
sds_tx_and_wait_ack (struct backend *backend, GByteArray *tx_msg,
		     guint packet, gint timeout, gint timeout2)
{
  gint err;
  gint t;
  guint rx_packet;
  GByteArray *rx_msg;
  gboolean waiting = FALSE;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, timeout);
  if (!rx_msg)
    {
      return -ETIMEDOUT;	//Nothing was received
    }

  t = timeout2;
  while (1)
    {
      rx_packet = rx_msg->data[4];
      rx_msg->data[4] = 0;

      if (!memcmp (rx_msg->data, SDS_WAIT, sizeof (SDS_WAIT)) && !waiting)
	{
	  debug_print (2, "WAIT received. Waiting for an ACK...");
	  t = SDS_NO_SPEC_TIMEOUT;
	  waiting = TRUE;
	}
      else if (!memcmp (rx_msg->data, SDS_ACK, sizeof (SDS_ACK)))
	{
	  err = 0;
	  break;
	}
      else if (!memcmp (rx_msg->data, SDS_NAK, sizeof (SDS_NAK)))
	{
	  err = -EBADMSG;
	  break;
	}
      else if (!memcmp (rx_msg->data, SDS_CANCEL, sizeof (SDS_CANCEL)))
	{
	  err = -ECANCELED;
	  break;
	}
      else if (rx_packet != packet)
	{
	  err = -EINVAL;	//Unexpected package number
	  break;
	}
      else
	{
	  err = -EIO;		//Message received but unrecognized
	  break;
	}

      free_msg (rx_msg);
      rx_msg = sds_rx (backend, t);
      if (!rx_msg)
	{
	  return -ENOMSG;
	}
    }

  free_msg (rx_msg);
  return err;
}

static inline GByteArray *
sds_get_data_packet_msg (gint packet, guint words, guint *word,
			 gint16 **frame, guint bits, guint bytes_per_word)
{
  guint8 *data;
  GByteArray *tx_msg = g_byte_array_sized_new (SDS_DATA_PACKET_LEN);
  g_byte_array_append (tx_msg, SDS_DATA_PACKET_HEADER,
		       sizeof (SDS_DATA_PACKET_HEADER));
  g_byte_array_set_size (tx_msg, SDS_DATA_PACKET_LEN);
  tx_msg->data[4] = packet;
  memset (&tx_msg->data[sizeof (SDS_DATA_PACKET_HEADER)], 0,
	  SDS_DATA_PACKET_PAYLOAD_LEN);
  tx_msg->data[SDS_DATA_PACKET_LEN - 1] = 0xf7;
  data = &tx_msg->data[sizeof (SDS_DATA_PACKET_HEADER)];
  for (guint i = 0; i < SDS_DATA_PACKET_PAYLOAD_LEN; i += bytes_per_word)
    {
      if (*word < words)
	{
	  sds_set_gint16_value_left_just (data, bytes_per_word, bits,
					  **frame);
	  data += bytes_per_word;
	  (*frame)++;
	  (*word)++;
	}
    }
  tx_msg->data[SDS_DATA_PACKET_CKSUM_POS] = sds_checksum (tx_msg->data);
  return tx_msg;
}

static inline GByteArray *
sds_get_rename_sample_msg (guint id, const gchar *name)
{
  GByteArray *tx_msg;
  gchar *sanitized = common_get_sanitized_name (name, NULL, 0);
  size_t len = strlen (sanitized);
  guint8 total;

  len = len > SDS_SAMPLE_NAME_MAX_LEN ? SDS_SAMPLE_NAME_MAX_LEN : len;
  total = sizeof (SDS_SAMPLE_NAME_HEADER) + 2 + len;
  tx_msg = g_byte_array_sized_new (total);
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_HEADER,
		       sizeof (SDS_SAMPLE_NAME_HEADER));
  tx_msg->data[5] = id % 0x80;
  tx_msg->data[6] = id / 0x80;
  g_byte_array_append (tx_msg, (guint8 *) & len, 1);
  g_byte_array_append (tx_msg, (guint8 *) sanitized, len);
  g_byte_array_append (tx_msg, (guint8 *) "\xf7", 1);

  g_free (sanitized);
  return tx_msg;
}

static gint
sds_rename (struct backend *backend, const gchar *src, const gchar *dst)
{
  GByteArray *tx_msg, *rx_msg;
  guint id;
  gint err;
  debug_print (1, "Sending rename request...");
  err = common_slot_get_id_name_from_path (src, &id, NULL);
  if (err)
    {
      return err;
    }

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

  tx_msg = sds_get_rename_sample_msg (id, dst);
  err = -ENOSYS;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SDS_NO_SPEC_TIMEOUT);
  if (rx_msg)
    {
      err = 0;
      free_msg (rx_msg);
    }

  return err;
}

static gint
sds_upload (struct backend *backend, const gchar *path, struct idata *sample,
	    struct job_control *control, guint bits)
{
  gchar *name;
  GByteArray *tx_msg;
  gint16 *frame, *f;
  gboolean active, open_loop = FALSE;
  guint word, words, words_per_packet, id, packet = 0, packets, retries =
    0, w, bytes_per_word;
  gint err = 0, word_size;
  struct sds_data *sds_data = backend->data;
  struct sample_info *sample_info = sample->info;
  GByteArray *input = sample->content;

  control->parts = 1;
  control->part = 0;
  job_control_set_progress (control, 0.0);

  if (common_slot_get_id_name_from_path (path, &id, &name))
    {
      return -EINVAL;
    }

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  debug_print (1, "Sending dump header...");

  words = input->len >> 1;	//bytes to words (frames)
  word_size = (gint) ceil (bits / 8.0);
  bytes_per_word = (gint) ceil (bits / 7.0);
  words_per_packet = SDS_DATA_PACKET_PAYLOAD_LEN / bytes_per_word;
  packets = ceil (words / (double) words_per_packet);

  tx_msg = sds_get_dump_msg (id, words, sample_info, bits);
  //The first timeout should be SDS_SPEC_TIMEOUT_HANDSHAKE (2 s) but it is not enough sometimes.
  err = sds_tx_and_wait_ack (backend, tx_msg, 0, SDS_NO_SPEC_TIMEOUT,
			     SDS_NO_SPEC_TIMEOUT);
  if (err == -ENOMSG)
    {
      debug_print (2, "No packet received after a WAIT. Continuing...");
    }
  else if (err == -ETIMEDOUT)
    {
      //In case of no response, we can assume an open loop.
      debug_print (1, "Assuming open loop...");
      open_loop = TRUE;
    }
  else if (err)
    {
      goto cleanup;
    }

  debug_print (1, "Sending dump data...");

  word = 0;
  sds_debug_print_sample_data (bits, bytes_per_word,
			       word_size, sample_info->rate, words, packets);
  frame = (gint16 *) input->data;
  while (packet < packets && active)
    {
      if (retries)
	{
	  usleep (sds_data->rest_time);
	}

      if (retries == SDS_MAX_RETRIES)
	{
	  debug_print (1, "Too many retries");
	  break;
	}

      f = frame;
      w = word;
      tx_msg = sds_get_data_packet_msg (packet % 0x80, words, &w, &f, bits,
					bytes_per_word);
      if (open_loop)
	{
	  err = backend_tx (backend, tx_msg);
	  usleep (SDS_NO_SPEC_OPEN_LOOP_REST_TIME);
	}
      else
	{
	  //SDS_SPEC_TIMEOUT is too low to be used here.
	  err = sds_tx_and_wait_ack (backend, tx_msg, packet % 0x80,
				     SDS_NO_SPEC_TIMEOUT,
				     SDS_NO_SPEC_TIMEOUT);
	}

      if (err == -EBADMSG)
	{
	  debug_print (2, "NAK received. Retrying...");
	  retries++;
	  continue;
	}
      else if (err == -ENOMSG)
	{
	  debug_print (2, "No packet received after a WAIT. Continuing...");
	  g_mutex_lock (&backend->mutex);
	  backend_rx_drain (backend);
	  g_mutex_unlock (&backend->mutex);
	}
      else if (err == -EINVAL)
	{
	  debug_print (2, "Unexpected packet number. Retrying...");
	  retries++;
	  continue;
	}
      else if (err == -ETIMEDOUT)
	{
	  debug_print (2, "No response. Retrying...");
	  retries++;
	  continue;
	}
      else if (err == -ECANCELED)
	{
	  debug_print (2, "Cancelled by device. Stopping...");
	  goto end;
	}
      else if (err)
	{
	  error_print ("Unhandled error");
	  goto end;
	}

      job_control_set_progress (control, packet / (gdouble) packets);
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      word = w;
      frame = f;
      packet++;
      retries = 0;
      err = 0;

      usleep (sds_data->rest_time);
    }

  if (active && sds_data->name_extension)
    {
      sds_rename (backend, path, name);
    }

end:
  if (active && packet == packets)
    {
      job_control_set_progress (control, 1.0);
    }
  else
    {
      debug_print (2, "Cancelling SDS upload...");
      sds_tx_handshake (backend, SDS_CANCEL, packet % 0x80);
      err = -ECANCELED;
    }

cleanup:
  g_free (name);
  return err;
}

static gint
sds_upload_8b (struct backend *backend, const gchar *path,
	       struct idata *sample, struct job_control *control)
{
  return sds_upload (backend, path, sample, control, 8);
}

static gint
sds_upload_12b (struct backend *backend, const gchar *path,
		struct idata *sample, struct job_control *control)
{
  return sds_upload (backend, path, sample, control, 12);
}

static gint
sds_upload_14b (struct backend *backend, const gchar *path,
		struct idata *sample, struct job_control *control)
{
  return sds_upload (backend, path, sample, control, 14);
}

static gint
sds_upload_16b (struct backend *backend, const gchar *path,
		struct idata *sample, struct job_control *control)
{
  return sds_upload (backend, path, sample, control, 16);
}

static gint
sds_next_sample_dentry (struct item_iterator *iter)
{
  struct sds_iterator_data *iterator_data = iter->data;
  struct sds_data *data = iterator_data->backend->data;

  if (iterator_data->next >= SDS_SAMPLE_LIMIT)
    {
      return -ENOENT;
    }

  iter->item.id = iterator_data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = -1;
  (iterator_data->next)++;

  if (data->name_extension)
    {
      gchar *name = sds_get_sample_name (iterator_data->backend,
					 iterator_data->next);
      snprintf (iter->item.name, LABEL_MAX, "%s", name ? name : "");
      g_free (name);
    }
  else
    {
      iter->item.name[0] = 0;
    }

  return 0;
}

static gint
sds_read_dir (struct backend *backend, struct item_iterator *iter,
	      const gchar *dir, GSList *extensions)
{
  struct sds_iterator_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct sds_iterator_data));
  data->next = 0;
  data->backend = backend;

  item_iterator_init (iter, dir, data, sds_next_sample_dentry, g_free);

  return 0;
}

static gint
sds_sample_load_common (const gchar *path, struct idata *sample,
			struct job_control *control, gint32 rate)
{
  return common_sample_load (path, sample, control, rate, SDS_SAMPLE_CHANNELS,
			     SF_FORMAT_PCM_16);
}

static gint
sds_sample_load (const gchar *path, struct idata *sample,
		 struct job_control *control)
{
  return sds_sample_load_common (path, sample, control, 0);	// Any sample rate is valid.
}

static gint
sds_sample_load_441 (const gchar *path, struct idata *sample,
		     struct job_control *control)
{
  return sds_sample_load_common (path, sample, control, 44100);
}

static gint
sds_sample_load_32 (const gchar *path, struct idata *sample,
		    struct job_control *control)
{
  return sds_sample_load_common (path, sample, control, 32000);
}

static gint
sds_sample_load_16 (const gchar *path, struct idata *sample,
		    struct job_control *control)
{
  return sds_sample_load_common (path, sample, control, 16000);
}

static gint
sds_sample_load_8 (const gchar *path, struct idata *sample,
		   struct job_control *control)
{
  return sds_sample_load_common (path, sample, control, 8000);
}

static gint
sds_sample_save (const gchar *path, struct idata *sample,
		 struct job_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

enum sds_fs
{
  FS_SAMPLES_SDS_16_B = 1,	//SDS devices also include FS_PROGRAM_DEFAULT_OPERATIONS as the first filesystem.
  FS_SAMPLES_SDS_14_B,
  FS_SAMPLES_SDS_12_B,
  FS_SAMPLES_SDS_8_B,
  FS_SAMPLES_SDS_16_B_441,
  FS_SAMPLES_SDS_16_B_32,
  FS_SAMPLES_SDS_16_B_16,
  FS_SAMPLES_SDS_16_B_8
};

static const struct fs_operations FS_SAMPLES_SDS_8B_OPERATIONS = {
  .id = FS_SAMPLES_SDS_8_B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "8b1c",
  .gui_name = "8 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_8b,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_12B_OPERATIONS = {
  .id = FS_SAMPLES_SDS_12_B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "12b1c",
  .gui_name = "12 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_12b,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_14B_OPERATIONS = {
  .id = FS_SAMPLES_SDS_14_B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "14b1c",
  .gui_name = "14 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_14b,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_16B_OPERATIONS = {
  .id = FS_SAMPLES_SDS_16_B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "16b1c",
  .gui_name = "16 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_16B_441_OPERATIONS = {
  .id = FS_SAMPLES_SDS_16_B_441,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "44.1k16b1c",
  .gui_name = "44.1 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load_441,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_16B_32_OPERATIONS = {
  .id = FS_SAMPLES_SDS_16_B_32,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "32k16b1c",
  .gui_name = "32 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load_32,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_16B_16_OPERATIONS = {
  .id = FS_SAMPLES_SDS_16_B_16,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "16k16b1c",
  .gui_name = "16 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load_16,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static const struct fs_operations FS_SAMPLES_SDS_16B_8_OPERATIONS = {
  .id = FS_SAMPLES_SDS_16_B_8,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID |
    FS_OPTION_SHOW_ID_COLUMN,
  .name = "8k16b1c",
  .gui_name = "8 KHz 16 bits mono",
  .gui_icon = FS_ICON_WAVE,
  .max_name_len = SDS_SAMPLE_NAME_MAX_LEN,
  .readdir = sds_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load_8,
  .save = sds_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path
};

static gint
sds_handshake_elektron (struct backend *backend)
{
  //Elektron devices support SDS so we need to be sure it is not.
  GByteArray *rx_msg = elektron_ping (backend);
  if (rx_msg)
    {
      free_msg (rx_msg);	//This is filled up by elektron_ping.
      g_free (backend->data);
      return -ENODEV;
    }

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

  return 0;
}

gint
sds_handshake_name (struct backend *backend)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (SDS_SAMPLE_NAME_REQUEST));
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_REQUEST,
		       sizeof (SDS_SAMPLE_NAME_REQUEST));
  tx_msg->data[5] = 1;
  tx_msg->data[6] = 0;
  GByteArray *rx_msg =
    backend_tx_and_rx_sysex (backend, tx_msg, SDS_NO_SPEC_TIMEOUT_TRY);
  if (rx_msg)
    {
      free_msg (rx_msg);
      return 0;
    }

  return -ENODEV;
}

gint
sds_handshake_loop_point (struct backend *backend)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (SDS_LOOP_POINT_REQUEST));
  g_byte_array_append (tx_msg, SDS_LOOP_POINT_REQUEST,
		       sizeof (SDS_LOOP_POINT_REQUEST));
  tx_msg->data[5] = 1;
  tx_msg->data[6] = 0;
  tx_msg->data[7] = 1;
  tx_msg->data[8] = 0;
  GByteArray *rx_msg =
    backend_tx_and_rx_sysex (backend, tx_msg, SDS_NO_SPEC_TIMEOUT_TRY);
  if (rx_msg)
    {
      free_msg (rx_msg);
      return 0;
    }

  return -ENODEV;
}

gint
sds_handshake_esi_2000 (struct backend *backend)
{
  //An upload to a real sample will erase the sample even if cancelled, so a sample id of a non existing slot is need.
  //We send a dump header for a number higher than every device might allow. Hopefully, this will fail on every device.
  //Numbers higher than 1500 make an E-Mu ESI-2000 crash when entering into the 'MIDI SAMPLE DUMP' menu but the actual limit is unknown.
  GByteArray *tx_msg = sds_get_dump_msg (1000, 0, NULL, 16);
  //In case we receive an ACK, NAK or CANCEL, there is a MIDI SDS device listening.
  gint err = sds_tx_and_wait_ack (backend, tx_msg, 0,
				  SDS_SPEC_TIMEOUT_HANDSHAKE,
				  SDS_NO_SPEC_TIMEOUT_TRY);
  if (err && err != -EBADMSG && err != -ECANCELED)
    {
      return -ENODEV;
    }

  //We cancel the upload.
  usleep (SDS_REST_TIME_DEFAULT);
  sds_tx_handshake (backend, SDS_CANCEL, 0);
  usleep (SDS_REST_TIME_DEFAULT);

  return 0;
}

gint
sds_handshake (struct backend *backend)
{
  gint err;
  gboolean name_extension;
  struct sds_data *sds_data;

  //We cancel anything that might be running.
  usleep (SDS_REST_TIME_DEFAULT);
  sds_tx_handshake (backend, SDS_CANCEL, 0);
  usleep (SDS_REST_TIME_DEFAULT);

  err = sds_handshake_elektron (backend);
  if (err)
    {
      return err;
    }

  err = sds_handshake_name (backend);
  if (err)
    {
      name_extension = FALSE;
    }
  else
    {
      name_extension = TRUE;
      goto end;
    }

  err = sds_handshake_loop_point (backend);
  if (!err)
    {
      goto end;
    }

  err = sds_handshake_esi_2000 (backend);
  if (err)
    {
      return err;
    }

end:
  debug_print (1, "Name extension: %s", name_extension ? "yes" : "no");

  //The remaining code is meant to set up different devices. These are the default values.

  sds_data = g_malloc (sizeof (struct sds_data));
  sds_data->rest_time = SDS_REST_TIME_DEFAULT;
  sds_data->name_extension = name_extension;

  g_slist_fill (&backend->fs_ops, &FS_PROGRAM_DEFAULT_OPERATIONS,
		&FS_SAMPLES_SDS_8B_OPERATIONS,
		&FS_SAMPLES_SDS_12B_OPERATIONS,
		&FS_SAMPLES_SDS_14B_OPERATIONS,
		&FS_SAMPLES_SDS_16B_OPERATIONS,
		&FS_SAMPLES_SDS_16B_441_OPERATIONS,
		&FS_SAMPLES_SDS_16B_32_OPERATIONS,
		&FS_SAMPLES_SDS_16B_16_OPERATIONS,
		&FS_SAMPLES_SDS_16B_8_OPERATIONS, NULL);
  backend->destroy_data = backend_destroy_data;
  backend->data = sds_data;

  if (!strlen (backend->name))
    {
      snprintf (backend->name, LABEL_MAX, "%s", _("SDS sampler"));
    }

  return 0;
}

const struct connector CONNECTOR_SDS = {
  .name = "sds",
  .handshake = sds_handshake,
  .standard = FALSE,
  .regex = NULL
};
