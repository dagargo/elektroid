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
#include "sample.h"
#include "sds.h"
#include "common.h"

#define SDS_SAMPLE_LIMIT 1000
#define SDS_DATA_PACKET_LEN 127
#define SDS_DATA_PACKET_PAYLOAD_LEN 120
#define SDS_DATA_PACKET_CKSUM_POS 125
#define SDS_DATA_PACKET_CKSUM_START 1
#define SDS_BYTES_PER_WORD 3
#define SDS_MAX_RETRIES 10
#define SDS_FIRST_ACK_WAIT_MS 2000
#define SDS_DEF_ACK_WAIT_MS 200
#define SDS_WAIT_MS 20
#define SDS_REST_TIME_US (SDS_WAIT_MS * 1000)
#define SDS_REST_TIME_LOOP_US 1000

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

static gchar *
sds_get_download_path (struct backend *backend,
		       struct item_iterator *remote_iter,
		       const struct fs_operations *ops, const gchar * dst_dir,
		       const gchar * src_path)
{
  GByteArray *tx_msg, *rx_msg;
  gchar *name = malloc (PATH_MAX);
  gchar *src_path_copy = strdup (src_path);
  gchar *filename = basename (src_path_copy);
  gint index = atoi (filename);

  tx_msg = g_byte_array_new ();
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_REQUEST,
		       sizeof (SDS_SAMPLE_NAME_REQUEST));
  tx_msg->data[5] = index % 128;
  tx_msg->data[6] = index / 128;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SDS_DEF_ACK_WAIT_MS);
  if (rx_msg)
    {
      snprintf (name, PATH_MAX, "%s/%s.wav", dst_dir, &rx_msg->data[5]);
      free_msg (rx_msg);
    }
  else
    {
      snprintf (name, PATH_MAX, "%s/%03d.wav", dst_dir, index);
    }

  g_free (src_path_copy);
  return name;
}

static guint
sds_get_bytes_value_right_just (guint8 * data, gint length)
{
  gint value = 0;
  for (gint i = 0, shift = 0; i < length; i++, shift += 7)
    {
      value |= data[i] << shift;
    }
  return value;
}

static void
sds_set_bytes_value_right_just (guint8 * data, gint length, guint value)
{
  for (gint i = 0, shift = 0; i < length; i++, shift += 7)
    {
      *data = 0x7f & (value >> shift);
      data++;
    }
}

static gint16
sds_get_gint16_value_left_just (guint8 * data, gint length, guint bits)
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
sds_set_gint16_value_left_just (guint8 * data, gint length, guint bits,
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
sds_checksum (guint8 * data)
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
sds_get_bytes_per_word (gint32 bits, guint * word_size,
			guint * bytes_per_word)
{
  *word_size = (guint) ceil (bits / 8.0);
  if (*word_size != 2)
    {
      error_print ("%d bits resolution not supported\n", bits);
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

static void
sds_tx (struct backend *backend, GByteArray * tx_msg)
{
  struct sysex_transfer transfer;
  transfer.raw = tx_msg;
  g_mutex_lock (&backend->mutex);
  backend_tx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  free_msg (tx_msg);
  //As stated by the specification, this should be 20 ms.
  usleep (SDS_REST_TIME_US);
}

static void
sds_tx_handshake (struct backend *backend, const guint8 * msg,
		  guint8 packet_num)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (SDS_ACK));
  g_byte_array_append (tx_msg, msg, sizeof (SDS_ACK));
  tx_msg->data[4] = packet_num;
  sds_tx (backend, tx_msg);
}

static guint
sds_get_download_info (GByteArray * header, struct sample_info *sample_info,
		       guint * words, guint * word_size,
		       guint * bytes_per_word)
{
  sample_info->bitdepth = header->data[6];
  if (sds_get_bytes_per_word (sample_info->bitdepth, word_size,
			      bytes_per_word))
    {
      return -1;
    }
  sample_info->samplerate =
    1.0e9 / sds_get_bytes_value_right_just (&header->data[7],
					    SDS_BYTES_PER_WORD);
  *words =
    sds_get_bytes_value_right_just (&header->data[10], SDS_BYTES_PER_WORD);
  sample_info->loopstart =
    sds_get_bytes_value_right_just (&header->data[13], SDS_BYTES_PER_WORD);
  sample_info->loopend =
    sds_get_bytes_value_right_just (&header->data[16], SDS_BYTES_PER_WORD);
  sample_info->looptype = header->data[19];
  return 0;
}

static inline void
sds_set_message_id (GByteArray * tx_msg, guint id)
{
  tx_msg->data[4] = id % 128;
  tx_msg->data[5] = id / 128;
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
      period = 1.0e9 / sample_info->samplerate;
      sds_set_bytes_value_right_just (&tx_msg->data[7], SDS_BYTES_PER_WORD,
				      period);
      sds_set_bytes_value_right_just (&tx_msg->data[10], SDS_BYTES_PER_WORD,
				      frames);
      sds_set_bytes_value_right_just (&tx_msg->data[13], SDS_BYTES_PER_WORD,
				      sample_info->loopstart);
      sds_set_bytes_value_right_just (&tx_msg->data[16], SDS_BYTES_PER_WORD,
				      sample_info->loopend);
      tx_msg->data[19] = (sample_info->loopstart == sample_info->loopend
			  && sample_info->loopstart ==
			  frames - 1) ? 0x7f : sample_info->looptype;
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

  while (transfer.raw && transfer.raw->len == 2
	 && !memcmp (transfer.raw->data, "\xf0\xf7", 2))
    {
      free_msg (transfer.raw);
      backend_rx_sysex (backend, &transfer);
    }
  g_mutex_unlock (&backend->mutex);
  return transfer.raw;
}

static void
sds_download_inc_packet (gboolean * first, guint * packet)
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
sds_debug_print_sample_data (guint bitdepth, guint bytes_per_word,
			     guint word_size, guint sample_rate, guint words,
			     guint packets)
{
  debug_print (1,
	       "Resolution: %d bits; %d bytes per word; word size %d bytes.\n",
	       bitdepth, bytes_per_word, word_size);
  debug_print (1, "Sample rate: %d Hz\n", sample_rate);
  debug_print (1, "Words: %d\n", words);
  debug_print (1, "Packets: %d\n", packets);
}

enum sds_last_packet_status
{
  SDS_LAST_PACKET_STATUS_ACK,
  SDS_LAST_PACKET_STATUS_NAK,
  SDS_LAST_PACKET_STATUS_TIMEOUT
};

static gint
sds_download (struct backend *backend, const gchar * path,
	      GByteArray * output, struct job_control *control)
{
  guint id, words, word_size, read_bytes, bytes_per_word, total_words, err,
    retries, packets, packet_num, exp_packet_num, rx_packets;
  gint16 sample;
  GByteArray *tx_msg, *rx_msg, *expected;
  gchar *path_copy, *index;
  guint8 *dataptr;
  gboolean active, first;
  enum sds_last_packet_status last_packet_status;
  struct sample_info *sample_info;
  struct sysex_transfer transfer;

  path_copy = strdup (path);
  index = basename (path_copy);
  id = atoi (index);
  g_free (path_copy);

  debug_print (1, "Sending dump request...\n");

  tx_msg = sds_get_request_msg (id);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);	//Default wait time

  //We skip the packets until we found a dump header.
  retries = 0;
  expected = sds_get_dump_msg (id, 0, NULL, 0);	//Any value here is valid
  while ((!rx_msg || rx_msg->len != sizeof (SDS_DUMP_HEADER)
	  || strncmp ((char *) rx_msg->data, (char *) expected->data, 6))
	 && retries < SDS_MAX_RETRIES)
    {
      debug_print (1, "Bad dump header. Skipping...\n");
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
      rx_msg = sds_rx (backend, SDS_WAIT_MS);
      retries++;
    }
  free_msg (expected);

  if (!rx_msg)
    {
      error_print ("No dump header\n");
      return -EIO;
    }

  sample_info = malloc (sizeof (struct sample_info));
  if (sds_get_download_info (rx_msg, sample_info, &words, &word_size,
			     &bytes_per_word))
    {
      free_msg (rx_msg);
      g_free (sample_info);
      return -EINVAL;
    }

  packets =
    ceil (words / (double) (SDS_DATA_PACKET_PAYLOAD_LEN / bytes_per_word));
  sds_debug_print_sample_data (sample_info->bitdepth, bytes_per_word,
			       word_size, sample_info->samplerate, words,
			       packets);

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);
  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);
  control->data = sample_info;

  debug_print (1, "Receiving dump data...\n");

  tx_msg = g_byte_array_new ();
  total_words = 0;
  retries = 0;
  last_packet_status = SDS_LAST_PACKET_STATUS_ACK;
  err = 0;
  packet_num = 0;
  exp_packet_num = 0;
  first = TRUE;
  rx_packets = 0;
  while (active && rx_packets < packets && packet_num < packets)
    {
      if (retries == SDS_MAX_RETRIES)
	{
	  error_print ("Too many retries\n");
	  break;
	}

      g_byte_array_set_size (tx_msg, 0);
      if (last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	{
	  g_byte_array_append (tx_msg, SDS_ACK, sizeof (SDS_ACK));
	}
      else
	{
	  g_byte_array_append (tx_msg, SDS_NAK, sizeof (SDS_NAK));
	}
      tx_msg->data[4] = packet_num % 128;

      if (packet_num == packets - 1
	  && last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	{
	  sds_tx (backend, tx_msg);
	  tx_msg = NULL;	//The message is already freed
	}
      else
	{
	  //The sampler might reach the timeout and send no packet but it's in its queue.
	  transfer.raw = tx_msg;
	  transfer.timeout = SDS_WAIT_MS;
	  err = backend_tx_and_rx_sysex_transfer (backend, &transfer, FALSE);
	  if (err == -ECANCELED)
	    {
	      break;
	    }
	  rx_msg = transfer.raw;
	  if (last_packet_status == SDS_LAST_PACKET_STATUS_TIMEOUT && rx_msg)
	    {
	      while (sds_rx (backend, SDS_WAIT_MS));
	    }
	}

      if (last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	{
	  sds_tx_handshake (backend, SDS_WAIT, (packet_num + 1) % 128);
	}

      if (!rx_msg || rx_msg->len < SDS_DATA_PACKET_LEN)
	{
	  if (last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	    {
	      sds_download_inc_packet (&first, &packet_num);
	    }
	  last_packet_status = SDS_LAST_PACKET_STATUS_TIMEOUT;
	  retries++;
	  continue;
	}

      if (rx_msg->len > SDS_DATA_PACKET_LEN)
	{
	  debug_print (2, "Invalid packet length. Stopping...\n");
	  err = -EINVAL;
	  free_msg (rx_msg);
	  break;
	}

      if (sds_checksum (rx_msg->data) !=
	  rx_msg->data[SDS_DATA_PACKET_CKSUM_POS])
	{
	  debug_print (2, "Invalid cksum. Retrying...\n");
	  free_msg (rx_msg);
	  last_packet_status = SDS_LAST_PACKET_STATUS_NAK;
	  retries++;
	  continue;
	}

      guint exp_packet_num_id = exp_packet_num % 128;
      if (rx_msg->data[4] != exp_packet_num_id)
	{
	  //Sometimes the device skips a packet number but sends the right amount of packets.
	  debug_print (2,
		       "Unexpected packet number (%d != %d). Continuing...\n",
		       rx_msg->data[4], exp_packet_num_id);
	  free_msg (rx_msg);
	  if (last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	    {
	      sds_download_inc_packet (&first, &packet_num);
	    }
	  last_packet_status = SDS_LAST_PACKET_STATUS_NAK;
	  retries++;
	  continue;
	}

      if (last_packet_status == SDS_LAST_PACKET_STATUS_ACK)
	{
	  sds_download_inc_packet (&first, &packet_num);
	}

      exp_packet_num++;
      rx_packets++;

      last_packet_status = SDS_LAST_PACKET_STATUS_ACK;
      retries = 0;

      read_bytes = 0;
      dataptr = &rx_msg->data[5];
      while (read_bytes < SDS_DATA_PACKET_PAYLOAD_LEN)
	{
	  sample = sds_get_gint16_value_left_just (dataptr,
						   bytes_per_word,
						   sample_info->bitdepth);
	  g_byte_array_append (output, (guint8 *) & sample, sizeof (sample));
	  dataptr += bytes_per_word;
	  read_bytes += bytes_per_word;
	  total_words++;
	}

      set_job_control_progress (control, rx_packets / (double) packets);

      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      free_msg (rx_msg);

      usleep (SDS_REST_TIME_US);
    }

  free_msg (tx_msg);

  usleep (SDS_REST_TIME_US);

  if (active && !err && rx_packets == packets)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      debug_print (1, "Cancelling SDS download...\n");
      sds_tx_handshake (backend, SDS_CANCEL, packet_num);
    }

  backend_rx_drain (backend);

  return err;
}

static gint
sds_tx_and_wait_ack (struct backend *backend, GByteArray * tx_msg,
		     guint packet_num, gint timeout)
{
  gint err;
  guint rx_packet_num;
  GByteArray *rx_msg;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, timeout);
  if (!rx_msg)
    {
      return -ETIMEDOUT;	//Nothing was received
    }

  rx_packet_num = rx_msg->data[4];
  while (rx_packet_num != packet_num)
    {
      debug_print (2, "Unexpected packet number. Skipping...\n");
      GByteArray *rx_next = sds_rx (backend, SDS_WAIT_MS);
      free_msg (rx_msg);
      rx_msg = rx_next;
      if (rx_next)
	{
	  rx_packet_num = rx_msg->data[4];
	}
      else
	{
	  debug_print (2, "No more packets\n");
	  return -ENOMSG;
	}
    }

  while (1)
    {
      // The SDS protocal states that we should wait indefinitely but 5 s is enough.
      rx_msg = sds_rx (backend, SYSEX_TIMEOUT_MS);
      if (!rx_msg)
	{
	  return -ENOMSG;
	}

      rx_packet_num = rx_msg->data[4];
      rx_msg->data[4] = 0;

      if (!memcmp (rx_msg->data, SDS_WAIT, sizeof (SDS_WAIT)))
	{
	  debug_print (2, "WAIT received. Waiting for an ACK...\n");
	  free_msg (rx_msg);
	  continue;
	}
      else if (!memcmp (rx_msg->data, SDS_ACK, sizeof (SDS_ACK)))
	{
	  err = 0;
	}
      else if (!memcmp (rx_msg->data, SDS_NAK, sizeof (SDS_NAK)))
	{
	  err = -EBADMSG;
	}
      else if (!memcmp (rx_msg->data, SDS_CANCEL, sizeof (SDS_CANCEL)))
	{
	  err = -ECANCELED;
	}
      else if (rx_packet_num != packet_num)
	{
	  err = -EINVAL;	//Unexpected package number
	}
      else
	{
	  err = -EIO;		//Message received but unrecognized
	}

      break;
    }

  free_msg (rx_msg);
  return err;
}

static inline GByteArray *
sds_get_data_packet_msg (gint packet_num, guint words, guint * word,
			 gint16 ** frame, guint bits, guint bytes_per_word)
{
  guint8 *data;
  GByteArray *tx_msg = g_byte_array_sized_new (SDS_DATA_PACKET_LEN);
  g_byte_array_append (tx_msg, SDS_DATA_PACKET_HEADER,
		       sizeof (SDS_DATA_PACKET_HEADER));
  g_byte_array_set_size (tx_msg, SDS_DATA_PACKET_LEN);
  tx_msg->data[4] = packet_num;
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
sds_get_rename_sample_msg (guint id, gchar * name)
{
  GByteArray *tx_msg = g_byte_array_new ();
  guint name_len = strlen (name);
  name_len = name_len > 127 ? 127 : name_len;
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_HEADER,
		       sizeof (SDS_SAMPLE_NAME_HEADER));
  tx_msg->data[5] = id % 128;
  tx_msg->data[6] = id / 128;
  g_byte_array_append (tx_msg, (guint8 *) & name_len, 1);
  g_byte_array_append (tx_msg, (guint8 *) name, name_len);
  g_byte_array_append (tx_msg, (guint8 *) "\xf7", 1);
  return tx_msg;
}

static gint
sds_rename (struct backend *backend, const gchar * src, const gchar * dst)
{
  GByteArray *tx_msg, *rx_msg;
  guint id;
  gint err;
  gchar *name;
  debug_print (1, "Sending rename request...\n");
  err = common_slot_get_id_name_from_path (dst, &id, &name);
  if (err)
    {
      return err;
    }

  tx_msg = sds_get_rename_sample_msg (id, name);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SDS_DEF_ACK_WAIT_MS);
  if (rx_msg)
    {
      free_msg (rx_msg);
    }

  return 0;
}

static gint
sds_upload (struct backend *backend, const gchar * path, GByteArray * input,
	    struct job_control *control, guint bits, guint bytes_per_word)
{
  GByteArray *tx_msg;
  gint16 *frame, *f;
  gboolean active, open_loop = FALSE;
  guint word, words, words_per_packet, id, packet = 0, packets, retries =
    0, w;
  gint err = 0, word_size;
  struct sample_info *sample_info = control->data;

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  if (common_slot_get_id_name_from_path (path, &id, NULL))
    {
      return -EBADSLT;
    }

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  debug_print (1, "Sending dump header...\n");

  tx_msg = sds_get_dump_msg (id, input->len >> 1, sample_info, bits);
  //The protocol states that we should wait for at least 2 s to let the receiver time.
  err = sds_tx_and_wait_ack (backend, tx_msg, 0, SDS_FIRST_ACK_WAIT_MS);
  if (err == -ENOMSG)
    {
      debug_print (2, "No packet received after a WAIT. Continuing...\n");
    }
  else if (err == -ETIMEDOUT)
    {
      //In case of no response, we can assume an open loop.
      debug_print (1, "Assuming open loop...\n");
      open_loop = TRUE;
    }
  else if (err)
    {
      return err;
    }

  debug_print (1, "Sending dump data...\n");

  word = 0;
  words = input->len >> 1;	//bytes to words (frames)
  words_per_packet = SDS_DATA_PACKET_PAYLOAD_LEN / SDS_BYTES_PER_WORD;
  packets = ceil (words / (double) words_per_packet);
  word_size = (gint) ceil (bits / 8.0);
  sds_debug_print_sample_data (bits, bytes_per_word,
			       word_size, sample_info->samplerate, words,
			       packets);
  frame = (gint16 *) input->data;
  while (packet < packets && active)
    {
      if (retries == SDS_MAX_RETRIES)
	{
	  error_print ("Too many retries\n");
	  break;
	}

      f = frame;
      w = word;
      tx_msg = sds_get_data_packet_msg (packet % 128, words, &w, &f, bits,
					bytes_per_word);
      if (open_loop)
	{
	  sds_tx (backend, tx_msg);
	}
      else
	{
	  //As stated by the specification, this should be 20 ms but it's not enough for some devices.
	  err = sds_tx_and_wait_ack (backend, tx_msg, packet % 128,
				     SDS_DEF_ACK_WAIT_MS);
	}

      if (err == -EBADMSG)
	{
	  debug_print (2, "NAK received. Retrying...\n");
	  retries++;
	  continue;
	}
      else if (err == -ENOMSG)
	{
	  debug_print (2, "No packet received after a WAIT. Continuing...\n");
	}
      else if (err == -EINVAL)
	{
	  debug_print (2, "Unexpectd packet number. Continuing...\n");
	}
      else if (err == -ETIMEDOUT)
	{
	  debug_print (2, "No response. Continuing in open loop...\n");
	  open_loop = TRUE;
	}
      else if (err == -ECANCELED)
	{
	  debug_print (2, "Cancelled by device. Stopping...\n");
	  goto end;
	}
      else if (err)
	{
	  error_print ("Unhandled error");
	  goto end;
	}

      set_job_control_progress (control, packet / (gdouble) packets);
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      word = w;
      frame = f;
      packet++;
      retries = 0;

      usleep (SDS_REST_TIME_LOOP_US);
    }

  if (active)
    {
      sds_rename (backend, path, path);
      usleep (SDS_REST_TIME_US);
    }

end:
  if (active && packet == packets)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      debug_print (2, "Cancelling SDS upload...\n");
      sds_tx_handshake (backend, SDS_CANCEL, packet % 128);
    }

  return err;
}

static gint
sds_upload_8b (struct backend *backend, const gchar * path,
	       GByteArray * input, struct job_control *control)
{
  return sds_upload (backend, path, input, control, 8, 2);
}

static gint
sds_upload_12b (struct backend *backend, const gchar * path,
		GByteArray * input, struct job_control *control)
{
  return sds_upload (backend, path, input, control, 12, 2);
}

static gint
sds_upload_16b (struct backend *backend, const gchar * path,
		GByteArray * input, struct job_control *control)
{
  return sds_upload (backend, path, input, control, 16, 3);
}

static void
sds_free_iterator_data (void *iter_data)
{
  g_free (iter_data);
}

static guint
sds_next_dentry (struct item_iterator *iter)
{
  gint next = *((gint *) iter->data);
  if (next < SDS_SAMPLE_LIMIT)
    {
      iter->item.id = next;
      snprintf (iter->item.name, LABEL_MAX, "%03d", next);
      iter->item.type = ELEKTROID_FILE;
      iter->item.size = -1;
      (*((gint *) iter->data))++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
sds_read_dir (struct backend *backend, struct item_iterator *iter,
	      const gchar * path)
{
  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  iter->data = g_malloc (sizeof (guint));
  *((gint *) iter->data) = 0;
  iter->next = sds_next_dentry;
  iter->free = sds_free_iterator_data;
  return 0;
}

gint
sds_sample_load (const gchar * path, GByteArray * sample,
		 struct job_control *control)
{
  struct sample_info *sample_info = g_malloc (sizeof (struct sample_info));
  sample_info->samplerate = -1;
  control->data = sample_info;
  return sample_load_with_frames (path, sample, control, NULL);
}

static void
sds_print (struct item_iterator *iter)
{
  printf ("%c %s\n", iter->item.type, iter->item.name);
}

enum sds_fs
{
  FS_SAMPLES_SDS_8_B = 0x1,
  FS_SAMPLES_SDS_12_B = 0x2,
  FS_SAMPLES_SDS_16_B = 0x4
};

static const struct fs_operations FS_SAMPLES_SDS_8B_OPERATIONS = {
  .fs = FS_SAMPLES_SDS_8_B,
  .options = FS_OPTION_SHOW_AUDIO_PLAYER | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "sample8",
  .gui_name = "Samples (8 bits)",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = sds_read_dir,
  .print_item = sds_print,
  .mkdir = NULL,
  .delete = NULL,
  .rename = sds_rename,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = sds_download,
  .upload = sds_upload_8b,
  .getid = get_item_index,
  .load = sds_sample_load,
  .save = sample_save,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path,
  .type_ext = "wav"
};

static const struct fs_operations FS_SAMPLES_SDS_12B_OPERATIONS = {
  .fs = FS_SAMPLES_SDS_12_B,
  .options = FS_OPTION_SHOW_AUDIO_PLAYER | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "sample12",
  .gui_name = "Samples (12 bits)",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = sds_read_dir,
  .print_item = sds_print,
  .mkdir = NULL,
  .delete = NULL,
  .rename = sds_rename,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = sds_download,
  .upload = sds_upload_12b,
  .getid = get_item_index,
  .load = sds_sample_load,
  .save = sample_save,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path,
  .type_ext = "wav"
};

static const struct fs_operations FS_SAMPLES_SDS_16B_OPERATIONS = {
  .fs = FS_SAMPLES_SDS_16_B,
  .options = FS_OPTION_SHOW_AUDIO_PLAYER | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "sample16",
  .gui_name = "Samples (16 bits)",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = sds_read_dir,
  .print_item = sds_print,
  .mkdir = NULL,
  .delete = NULL,
  .rename = sds_rename,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = sds_download,
  .upload = sds_upload_16b,
  .getid = get_item_index,
  .load = sds_sample_load,
  .save = sample_save,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = sds_get_download_path,
  .type_ext = "wav"
};

static const struct fs_operations *FS_SDS_16B_OPERATIONS[] = {
  &FS_SAMPLES_SDS_16B_OPERATIONS, NULL
};

static const struct fs_operations *FS_SDS_12B_OPERATIONS[] = {
  &FS_SAMPLES_SDS_12B_OPERATIONS, NULL
};

static const struct fs_operations *FS_SDS_ALL_OPERATIONS[] = {
  &FS_SAMPLES_SDS_8B_OPERATIONS, &FS_SAMPLES_SDS_12B_OPERATIONS,
  &FS_SAMPLES_SDS_16B_OPERATIONS, NULL
};

gint
sds_handshake (struct backend *backend)
{
  //We send a dump header for the highest number allowed. Hopefully, this will fail on every device.
  GByteArray *tx_msg = sds_get_dump_msg (0x3fff, 0, NULL, 16);
  //In case we receive anything, there is a MIDI SDS device listening.
  gint err = sds_tx_and_wait_ack (backend, tx_msg, 0, SDS_FIRST_ACK_WAIT_MS);
  if (err == -EIO || err == -ETIMEDOUT)
    {
      return err;
    }

  //We cancel the upload.
  usleep (SDS_REST_TIME_US);
  sds_tx_handshake (backend, SDS_CANCEL, 0);

  backend->device_desc.filesystems =
    FS_SAMPLES_SDS_8_B | FS_SAMPLES_SDS_12_B | FS_SAMPLES_SDS_16_B;
  backend->fs_ops = FS_SDS_ALL_OPERATIONS;

  snprintf (backend->device_name, LABEL_MAX, "sampler (MIDI SDS)");

  return 0;
}
