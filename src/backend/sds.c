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

#define SDS_SAMPLE_LIMIT 1000
#define SDS_DATA_PACKET_LEN 127
#define SDS_DATA_PACKET_PAYLOAD_LEN 120
#define SDS_DATA_PACKET_CKSUM_POS 125
#define SDS_DATA_PACKET_CKSUM_START 1
#define SDS_BYTES_PER_WORD 3
#define SDS_ACK_WAIT_TIME_MS 5000
#define SDS_BITS 16

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

static const guint8 MIDI_IDENTITY_REQUEST[] =
  { 0xf0, 0x7e, 0x7f, 6, 1, 0xf7 };

static gchar *
sds_get_upload_path (struct backend *backend,
		     struct item_iterator *remote_iter,
		     const struct fs_operations *ops, const gchar * dst_dir,
		     const gchar * src_path, gint32 * next_index)
{
  //In this case, dst_dir must include the index, ':' and the sample name.
  return strdup (dst_dir);
}

static gchar *
sds_get_download_path (struct backend *backend,
		       struct item_iterator *remote_iter,
		       const struct fs_operations *ops, const gchar * dst_dir,
		       const gchar * src_path)
{
  GByteArray *tx_msg, *rx_msg;
  gchar *name = malloc (LABEL_MAX);
  gchar *src_path_copy = strdup (src_path);
  gchar *filename = basename (src_path_copy);
  gint index = atoi (filename);

  tx_msg = g_byte_array_new ();
  g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_REQUEST,
		       sizeof (SDS_SAMPLE_NAME_REQUEST));
  tx_msg->data[5] = index % 128;
  tx_msg->data[6] = index / 128;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
  if (rx_msg)
    {
      snprintf (name, LABEL_MAX, "%s/%s.wav", dst_dir, &rx_msg->data[5]);
      free_msg (rx_msg);
    }
  else
    {
      snprintf (name, LABEL_MAX, "%s/%d.wav", dst_dir, index);
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

static GByteArray *
sds_rx_handshake (struct backend *backend)
{
  struct sysex_transfer transfer;
  transfer.active = TRUE;
  transfer.timeout = SDS_ACK_WAIT_TIME_MS;
  transfer.batch = FALSE;
  g_mutex_lock (&backend->mutex);
  backend_rx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  return transfer.raw;
}

static void
sds_tx_handshake (struct backend *backend, const guint8 * msg, guint len,
		  guint8 packet_num)
{
  struct sysex_transfer transfer;
  transfer.active = TRUE;
  transfer.timeout = SDS_ACK_WAIT_TIME_MS;
  transfer.raw = g_byte_array_sized_new (len);
  g_byte_array_append (transfer.raw, msg, len);
  transfer.raw->data[4] = packet_num;
  g_mutex_lock (&backend->mutex);
  backend_tx_sysex (backend, &transfer);
  g_mutex_unlock (&backend->mutex);
  free_msg (transfer.raw);
}

static gint
sds_download (struct backend *backend, const gchar * path,
	      GByteArray * output, struct job_control *control)
{
  guint id, period, words, packet_counter, word_size, read_bytes,
    bytes_per_word, total_words;
  gint16 sample;
  double samplerate;
  GByteArray *tx_msg, *rx_msg;
  gchar *path_copy, *index;
  guint8 *dataptr;
  gboolean active, header_resp;
  struct sample_info *sample_info;

  path_copy = strdup (path);
  index = basename (path_copy);
  id = atoi (index);
  g_free (path_copy);

  tx_msg = g_byte_array_new ();
  g_byte_array_append (tx_msg, SDS_SAMPLE_REQUEST,
		       sizeof (SDS_SAMPLE_REQUEST));
  tx_msg->data[4] = id % 128;
  tx_msg->data[5] = id / 128;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  sample_info = malloc (sizeof (struct sample_info));
  sample_info->bitdepth = rx_msg->data[6];
  if (sds_get_bytes_per_word
      (sample_info->bitdepth, &word_size, &bytes_per_word))
    {
      free_msg (rx_msg);
      return -EINVAL;
    }

  debug_print (1,
	       "Resolution: %d bits; %d bytes per word; word size %d bytes.\n",
	       sample_info->bitdepth, bytes_per_word, word_size);

  period =
    sds_get_bytes_value_right_just (&rx_msg->data[7], SDS_BYTES_PER_WORD);
  samplerate = 1.0e9 / period;
  debug_print (1, "Sample rate: %.1f Hz (period %d ns)\n", samplerate,
	       period);

  words =
    sds_get_bytes_value_right_just (&rx_msg->data[10], SDS_BYTES_PER_WORD);
  debug_print (1, "Words: %d\n", words);

  sample_info->samplerate = samplerate;
  sample_info->loopstart =
    sds_get_bytes_value_right_just (&rx_msg->data[13], SDS_BYTES_PER_WORD);
  sample_info->loopend =
    sds_get_bytes_value_right_just (&rx_msg->data[16], SDS_BYTES_PER_WORD);
  sample_info->looptype = rx_msg->data[19];
  control->data = sample_info;
  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);
  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  packet_counter = 0;
  total_words = 0;
  header_resp = TRUE;
  while (total_words < words && active)
    {
      gboolean ok = TRUE;
      gint packet_num = header_resp ? 0 : packet_counter;
      gint next_packet_num = header_resp ? 0 : packet_counter + 1;
      next_packet_num = next_packet_num == 0x80 ? 0 : next_packet_num;
      gint errors = 0;
      while (errors < 10)
	{
	  tx_msg = g_byte_array_new ();
	  if (ok)
	    {
	      g_byte_array_append (tx_msg, SDS_ACK, sizeof (SDS_ACK));
	      tx_msg->data[4] = packet_num;
	    }
	  else
	    {
	      g_byte_array_append (tx_msg, SDS_NAK, sizeof (SDS_NAK));
	      tx_msg->data[4] = next_packet_num;
	    }
	  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
	  if (!rx_msg)
	    {
	      g_free (path_copy);
	      sds_tx_handshake (backend, SDS_CANCEL, sizeof (SDS_CANCEL),
				next_packet_num);
	      return -EIO;
	    }

	  if (rx_msg->len == SDS_DATA_PACKET_LEN
	      && rx_msg->data[4] == next_packet_num
	      && sds_checksum (rx_msg->data) ==
	      rx_msg->data[SDS_DATA_PACKET_CKSUM_POS])
	    {
	      //Add checksum code
	      if (header_resp)
		{
		  header_resp = FALSE;
		}
	      else
		{
		  packet_counter++;
		  if (packet_counter == 0x80)
		    {
		      packet_counter = 0;
		    }
		}

	      break;
	    }

	  error_print ("Package %d expected. NAK!\n", next_packet_num);
	  errors++;

	  free_msg (rx_msg);
	  ok = FALSE;

	  usleep (REST_TIME_US);
	}

      if (errors == 10)
	{
	  g_mutex_lock (&control->mutex);
	  control->active = FALSE;
	  g_mutex_unlock (&control->mutex);
	  active = FALSE;
	}

      read_bytes = 0;
      dataptr = &rx_msg->data[5];
      while (active && read_bytes < SDS_DATA_PACKET_PAYLOAD_LEN
	     && total_words < words)
	{
	  sample =
	    sds_get_gint16_value_left_just (dataptr, bytes_per_word,
					    sample_info->bitdepth);
	  g_byte_array_append (output, (guint8 *) & sample, sizeof (sample));

	  dataptr += bytes_per_word;
	  read_bytes += bytes_per_word;

	  total_words++;

	  if (control)
	    {
	      set_job_control_progress (control,
					total_words / (double) (words + 1));
	      g_mutex_lock (&control->mutex);
	      active = control->active;
	      g_mutex_unlock (&control->mutex);
	    }
	}

      usleep (REST_TIME_US);

      free_msg (rx_msg);
    }

  if (active)
    {
      sds_tx_handshake (backend, SDS_ACK, sizeof (SDS_ACK), packet_counter);
      set_job_control_progress (control, 1.0);
    }
  else
    {
      debug_print (1, "Cancelling SDS download...\n");
      sds_tx_handshake (backend, SDS_CANCEL, sizeof (SDS_CANCEL),
			packet_counter);
      backend_rx_drain (backend);
    }

  return 0;
}

static gint
sds_upload_wait_ack (struct backend *backend, GByteArray * rx_msg,
		     guint packet_num)
{
  gint err;

  if (!rx_msg)
    {
      err = -EIO;
      goto end;
    }

  rx_msg->data[4] = 0;
  if (!memcmp (rx_msg->data, SDS_WAIT, sizeof (SDS_WAIT)))
    {
      debug_print (2, "Waiting for an ACK...\n");
      rx_msg = sds_rx_handshake (backend);
      if (!rx_msg)
	{
	  debug_print (2, "No ACK received. Sending next packet...\n");
	  return -ETIMEDOUT;
	}
    }

  if (rx_msg->len == sizeof (SDS_ACK) && rx_msg->data[4] == packet_num)
    {
      rx_msg->data[4] = 0;
      if (!memcmp (rx_msg->data, SDS_NAK, sizeof (SDS_NAK)))
	{
	  err = -EINVAL;
	}
      else if (!memcmp (rx_msg->data, SDS_CANCEL, sizeof (SDS_CANCEL)))
	{
	  err = -EIO;
	}
      else
	{
	  err = 0;
	}
    }
  else
    {
      err = -EIO;
    }

  free_msg (rx_msg);
end:
  usleep (REST_TIME_US);
  return err;
}

static gint
sds_upload (struct backend *backend, const gchar * path, GByteArray * input,
	    struct job_control *control)
{
  GByteArray *tx_msg, *rx_msg;
  gchar *path_copy, *index_name, *name;
  gint err = 0;
  gint packet_num = -1;
  gint16 *frame;
  gboolean active;
  struct sample_info *sample_info = control->data;
  guint name_len, word, words, words_per_packet, id, packets, period =
    1.0e9 / sample_info->samplerate;

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  path_copy = strdup (path);
  index_name = basename (path_copy);
  id = (gint) strtol (index_name, &name, 10);
  if (strncmp
      (name, SAMPLE_ID_NAME_SEPARATOR,
       strlen (SAMPLE_ID_NAME_SEPARATOR)) == 0)
    {
      name++;			//Skip ':'
    }
  else
    {
      error_print
	("Path name not provided properly. Proceeding without renaming...\n");
      name = NULL;
    }

  tx_msg = g_byte_array_sized_new (sizeof (SDS_DUMP_HEADER));
  g_byte_array_append (tx_msg, SDS_DUMP_HEADER, sizeof (SDS_DUMP_HEADER));
  tx_msg->data[4] = id % 128;
  tx_msg->data[5] = id / 128;
  tx_msg->data[6] = (guint8) sample_info->bitdepth;

  debug_print (1,
	       "Resolution: %d bits; %d bytes per word; word size %d bytes.\n",
	       sample_info->bitdepth, SDS_BYTES_PER_WORD, 2);
  debug_print (1, "Sample rate: %.1f Hz (period %d ns)\n",
	       (double) sample_info->samplerate, period);

  words = input->len >> 1;	//bytes to words (frames)
  frame = (gint16 *) input->data;
  sds_set_bytes_value_right_just (&tx_msg->data[7], SDS_BYTES_PER_WORD,
				  period);
  sds_set_bytes_value_right_just (&tx_msg->data[10], SDS_BYTES_PER_WORD,
				  words);
  sds_set_bytes_value_right_just (&tx_msg->data[13], SDS_BYTES_PER_WORD,
				  sample_info->loopstart);
  sds_set_bytes_value_right_just (&tx_msg->data[16], SDS_BYTES_PER_WORD,
				  sample_info->loopend);
  tx_msg->data[19] = sample_info->looptype;

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  err = sds_upload_wait_ack (backend, rx_msg, 0);
  if (err)
    {
      goto end;
    }

  words_per_packet = SDS_DATA_PACKET_PAYLOAD_LEN / SDS_BYTES_PER_WORD;
  packets = ceil (words / (double) words_per_packet);
  packet_num = 0;
  word = 0;
  debug_print (1, "Words: %d\n", words);
  debug_print (1, "Packets: %d\n", packets);
  guint i = 0;
  while (i < packets && active)
    {
      tx_msg = g_byte_array_sized_new (SDS_DATA_PACKET_LEN);
      g_byte_array_append (tx_msg, SDS_DATA_PACKET_HEADER,
			   sizeof (SDS_DATA_PACKET_HEADER));
      g_byte_array_set_size (tx_msg, SDS_DATA_PACKET_LEN);
      tx_msg->data[4] = packet_num;
      memset (&tx_msg->data[sizeof (SDS_DATA_PACKET_HEADER)], 0,
	      SDS_DATA_PACKET_PAYLOAD_LEN);
      tx_msg->data[SDS_DATA_PACKET_LEN - 1] = 0xf7;

      guint8 *data = &tx_msg->data[sizeof (SDS_DATA_PACKET_HEADER)];
      gint16 *prev_frame = frame;
      guint prev_word = word;
      for (guint j = 0; j < SDS_DATA_PACKET_PAYLOAD_LEN;
	   j += SDS_BYTES_PER_WORD)
	{
	  if (word < words)
	    {
	      sds_set_gint16_value_left_just (data, SDS_BYTES_PER_WORD,
					      SDS_BITS, *frame);
	      data += SDS_BYTES_PER_WORD;
	      frame++;
	      word++;
	    }
	}
      tx_msg->data[SDS_DATA_PACKET_CKSUM_POS] = sds_checksum (tx_msg->data);

      rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
      err = sds_upload_wait_ack (backend, rx_msg, packet_num);
      if (err == -EINVAL)	//NAK packet
	{
	  frame = prev_frame;
	  word = prev_word;
	  continue;
	}
      else if (err && err != -ETIMEDOUT)	//CANCEL packet
	{
	  goto end;
	}

      packet_num++;
      if (packet_num == 0x80)
	{
	  packet_num = 0;
	}

      i++;

      set_job_control_progress (control, i / (double) (packets + 1));
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);
    }

  if (*name && active)
    {
      tx_msg = g_byte_array_new ();
      g_byte_array_append (tx_msg, SDS_SAMPLE_NAME_HEADER,
			   sizeof (SDS_SAMPLE_NAME_HEADER));
      tx_msg->data[5] = id % 128;
      tx_msg->data[6] = id / 128;

      name_len = strlen (name);
      name_len = name_len > 127 ? 127 : name_len;
      g_byte_array_append (tx_msg, (guint8 *) & name_len, 1);
      g_byte_array_append (tx_msg, (guint8 *) name, name_len);

      g_byte_array_append (tx_msg, (guint8 *) "\xf7", 1);
      rx_msg =
	backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
      if (rx_msg)
	{
	  free_msg (rx_msg);
	}
    }

end:
  if (active)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      if (packet_num >= 0)
	{
	  debug_print (1, "Cancelling SDS upload...\n");
	  sds_tx_handshake (backend, SDS_CANCEL, sizeof (SDS_CANCEL),
			    packet_num);
	}
    }

  g_free (path_copy);
  return err;
}

static void
sds_free_iterator_data (void *iter_data)
{
  g_free (iter_data);
}

static guint
sds_next_dentry (struct item_iterator *iter)
{
  gint index = *((gint *) iter->data);

  if (iter->item.name != NULL)
    {
      g_free (iter->item.name);
    }

  if (index < SDS_SAMPLE_LIMIT)
    {
      iter->item.index = index;
      iter->item.name = g_malloc (LABEL_MAX);
      snprintf (iter->item.name, LABEL_MAX, "%d", index);
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
  iter->item.name = NULL;
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;

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
print_sds (struct item_iterator *iter)
{
  printf ("%c %s\n", iter->item.type, iter->item.name);
}

enum sds_fs
{
  FS_SAMPLES_SDS = 1
};

static const struct fs_operations FS_SAMPLES_SDS_OPERATIONS = {
  .fs = FS_SAMPLES_SDS,
  .options = FS_OPTION_SHOW_AUDIO_PLAYER | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "sds",
  .gui_name = "Samples",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = sds_read_dir,
  .print_item = print_sds,
  .mkdir = NULL,
  .delete = NULL,
  .rename = NULL,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = sds_download,
  .upload = sds_upload,
  .getid = get_item_index,
  .load = sds_sample_load,
  .save = sample_save,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = sds_get_upload_path,
  .get_download_path = sds_get_download_path,
  .type_ext = "wav"
};

static const struct fs_operations *FS_SDS_OPERATIONS[] = {
  &FS_SAMPLES_SDS_OPERATIONS, NULL
};

static gint
sds_handshake_internal (struct backend *backend)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint err = 0;

  tx_msg = g_byte_array_sized_new (sizeof (SDS_SAMPLE_REQUEST));
  g_byte_array_append (tx_msg, SDS_SAMPLE_REQUEST,
		       sizeof (SDS_SAMPLE_REQUEST));
  tx_msg->data[4] = 1;
  tx_msg->data[5] = 0;
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -EIO;
    }
  free_msg (rx_msg);

  sds_tx_handshake (backend, SDS_CANCEL, sizeof (SDS_CANCEL), 0);
  backend_rx_drain (backend);

  return err;
}

gint
sds_handshake (struct backend *backend)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  guint8 *company, *family, *model, *version;
  gint offset, err = 0;

  backend->device_desc.id = -1;
  backend->device_desc.storage = 0;
  backend->upgrade_os = NULL;
  backend->get_storage_stats = NULL;
  backend->device_desc.alias = NULL;
  backend->device_name = NULL;
  backend->fs_ops = NULL;
  backend->destroy_data = NULL;

  tx_msg = g_byte_array_sized_new (sizeof (MIDI_IDENTITY_REQUEST));
  //Identity Request Universal Sysex message
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
	      company = &rx_msg->data[5];
	      family = &rx_msg->data[6 + offset];
	      model = &rx_msg->data[8 + offset];
	      version = &rx_msg->data[10 + offset];

	      backend->device_name = malloc (LABEL_MAX);
	      snprintf (backend->device_name, LABEL_MAX,
			"%02x-%02x-%02x %02x-%02x %02x-%02x %d.%d.%d.%d",
			company[0], offset ? company[1] : 0,
			offset ? company[2] : 0, family[0], family[1],
			model[0], model[1], version[0], version[1],
			version[2], version[3]);
	      backend->device_desc.name = strdup (backend->device_name);
	    }
	  else
	    {
	      error_print ("Illegal SUB-ID2\n");
	      err = -EIO;
	    }
	}
      else
	{
	  error_print ("Illegal SUB-ID2\n");
	  err = -EIO;
	}

      free_msg (rx_msg);
    }

  if (!err && !sds_handshake_internal (backend))
    {
      backend->device_desc.filesystems = FS_SAMPLES_SDS;

      backend->fs_ops = FS_SDS_OPERATIONS;
      backend->upgrade_os = NULL;

      if (!backend->device_name)
	{
	  backend->device_name = strdup ("MIDI SDS sampler");
	  backend->device_desc.name = strdup (backend->device_name);
	}
    }

  return err;
}
