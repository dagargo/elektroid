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
#include <endian.h>
#include <poll.h>
#include <zlib.h>
#include <libgen.h>
#include "connector.h"
#include "utils.h"

#define KB 1024
#define BUFF_SIZE (4 * KB)
#define RING_BUFF_SIZE (256 * KB)
#define SAMPLE_TRANSF_BLOCK_BYTES 0x2000
#define SAMPLE_TRANSF_BLOCK_SHORTS  (SAMPLE_TRANSF_BLOCK_BYTES / 2)
#define OS_TRANSF_BLOCK_BYTES 0x800
#define POLL_TIMEOUT 20
#define REST_TIME 50000

#define AFMK1_ID 0x04
#define AKEYS_ID 0x06
#define ARMK1_ID 0x08
#define AHMK1_ID 0x0a
#define DTAKT_ID 0x0c
#define AFMK2_ID 0x0e
#define ARMK2_ID 0x10
#define DTONE_ID 0x14
#define AHMK2_ID 0x16
#define DKEYS_ID 0x1c
#define MOD_S_ID 0x19

static const guint8 MSG_HEADER[] = { 0xf0, 0, 0x20, 0x3c, 0x10, 0 };

static const guint8 PING_REQUEST[] = { 0x1 };
static const guint8 SOFTWARE_VERSION_REQUEST[] = { 0x2 };
static const guint8 DEVICEUID_REQUEST[] = { 0x3 };
static const guint8 STORAGEINFO_REQUEST[] = { 0x5 };
static const guint8 FS_SAMPLE_READ_DIR_REQUEST[] = { 0x10 };
static const guint8 FS_SAMPLE_CREATE_DIR_REQUEST[] = { 0x11 };
static const guint8 FS_SAMPLE_DELETE_DIR_REQUEST[] = { 0x12 };
static const guint8 FS_SAMPLE_DELETE_FILE_REQUEST[] = { 0x20 };
static const guint8 FS_SAMPLE_RENAME_FILE_REQUEST[] = { 0x21 };
static const guint8 FS_SAMPLE_OPEN_FILE_READER_REQUEST[] = { 0x30 };
static const guint8 FS_SAMPLE_CLOSE_FILE_READER_REQUEST[] = { 0x31 };
static const guint8 FS_SAMPLE_READ_FILE_REQUEST[] =
  { 0x32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 FS_SAMPLE_OPEN_FILE_WRITER_REQUEST[] =
  { 0x40, 0, 0, 0, 0 };
static const guint8 FS_SAMPLE_CLOSE_FILE_WRITER_REQUEST[] =
  { 0x41, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 FS_SAMPLE_WRITE_FILE_REQUEST[] =
  { 0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 FS_SAMPLE_WRITE_FILE_REQUEST_PAD_1ST[] = {
  0, 0, 0, 0, 0, 0, 0xbb, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
  0x7f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const guint8 DATA_LIST_REQUEST[] = { 0x53 };
static const guint8 DATA_MOVE_REQUEST[] = { 0x5a };
static const guint8 DATA_COPY_REQUEST[] = { 0x5b };
static const guint8 DATA_CLEAR_REQUEST[] = { 0x5c };
static const guint8 DATA_SWAP_REQUEST[] = { 0x5d };
static const guint8 OS_UPGRADE_START_REQUEST[] =
  { 0x50, 0, 0, 0, 0, 's', 'y', 's', 'e', 'x', '\0', 1 };
static const guint8 OS_UPGRADE_WRITE_RESPONSE[] =
  { 0x51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static const gchar *FS_TYPE_NAMES[] = { "+Drive", "RAM" };

static const struct connector_device_desc ANALOG_RYTM_DESC = {
  .id = ARMK1_ID,
  .model = "Analog Rytm",
  .fss = FS_SAMPLES,
  .storages = STORAGE_PLUS_DRIVE | STORAGE_RAM
};

static const struct connector_device_desc DIGITAKT_DESC = {
  .id = DTAKT_ID,
  .model = "Digitakt",
  .fss = FS_SAMPLES | FS_DATA,
  .storages = STORAGE_PLUS_DRIVE | STORAGE_RAM
};

static const struct connector_device_desc ANALOG_RYTM_MKII_DESC = {
  .id = ARMK2_ID,
  .model = "Analog Rytm MKII",
  .fss = FS_SAMPLES,
  .storages = STORAGE_PLUS_DRIVE | STORAGE_RAM
};

static const struct connector_device_desc MODEL_SAMPLES_DESC = {
  .id = MOD_S_ID,
  .model = "Model:Samples",
  .fss = FS_SAMPLES,
  .storages = STORAGE_PLUS_DRIVE | STORAGE_RAM
};

static const struct connector_device_desc NULL_DEVICE_DESC = {
  .id = 0,
  .model = "-",
  .fss = 0,
  .storages = 0
};

static const struct connector_device_desc *CONNECTOR_DEVICE_DESCS[] = {
  &ANALOG_RYTM_DESC, &DIGITAKT_DESC, &ANALOG_RYTM_MKII_DESC,
  &MODEL_SAMPLES_DESC
};

static struct connector_fs_operations FS_SAMPLES_OPERATIONS = {
  .fs = FS_SAMPLES,
  .readdir = connector_read_samples,
  .mkdir = connector_create_samples_dir,
  .delete = connector_delete_samples_item,
  .move = connector_move_samples_item,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = connector_download_sample,
  .upload = connector_upload_sample,
  .getid = get_item_name
};

static struct connector_fs_operations FS_DATA_OPERATIONS = {
  .fs = FS_DATA,
  .readdir = connector_read_data,
  .mkdir = NULL,
  .delete = NULL,
  .move = connector_move_data_item,
  .copy = connector_copy_data_item,
  .clear = connector_clear_data_item,
  .swap = connector_swap_data_item,
  .download = NULL,
  .upload = NULL,
  .getid = get_item_index
};

static struct connector_fs_operations FS_NONE_OPERATIONS = {
  .fs = 0,
  .readdir = NULL,
  .mkdir = NULL,
  .delete = NULL,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = NULL,
  .upload = NULL,
  .getid = NULL
};

static const struct connector_fs_operations *FS_OPERATIONS[] = {
  &FS_SAMPLES_OPERATIONS, &FS_DATA_OPERATIONS
};

static const int FS_OPERATIONS_N =
  sizeof (FS_OPERATIONS) / sizeof (struct connector_fs_operations *);

static enum item_type connector_get_path_type (struct connector *,
					       const gchar *);

static void
connector_free_iterator_data (void *iter_data)
{
  struct connector_iterator_data *data = iter_data;

  free_msg (data->msg);
  g_free (data);
}

const struct connector_fs_operations *
connector_get_fs_operations (enum connector_fs fs)
{
  const struct connector_fs_operations *fs_operations = &FS_NONE_OPERATIONS;
  for (int i = 0; i < FS_OPERATIONS_N; i++)
    {
      if (FS_OPERATIONS[i]->fs == fs)
	{
	  fs_operations = FS_OPERATIONS[i];
	}
    }
  return fs_operations;
}

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

static guint
connector_next_sample_entry (struct item_iterator *iter)
{
  guint32 *data32;
  gchar *entry_cp1252;
  struct connector_iterator_data *data = iter->data;

  if (iter->entry != NULL)
    {
      g_free (iter->entry);
    }

  if (data->pos == data->msg->len)
    {
      iter->entry = NULL;
      return -ENOENT;
    }
  else
    {
      data32 = (guint32 *) & data->msg->data[data->pos];
      data->cksum = be32toh (*data32);
      data->pos += sizeof (guint32);

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->size = be32toh (*data32);
      data->pos += sizeof (guint32);

      data->pos += 1;		//write_protected

      iter->type = data->msg->data[data->pos];
      data->pos++;

      entry_cp1252 = (gchar *) & data->msg->data[data->pos];
      iter->entry =
	g_convert (entry_cp1252, -1, "UTF8", "CP1252", NULL, NULL, NULL);
      data->pos += strlen (entry_cp1252) + 1;

      iter->id = -1;

      return 0;
    }
}

static struct item_iterator *
connector_new_sample_iterator (GByteArray * msg)
{
  struct item_iterator *iter = malloc (sizeof (struct item_iterator));
  struct connector_iterator_data *data =
    malloc (sizeof (struct connector_iterator_data));

  data->msg = msg;
  data->pos = 5;

  iter->data = data;
  iter->entry = NULL;
  iter->next = connector_next_sample_entry;
  iter->free = connector_free_iterator_data;

  return iter;
}

static GByteArray *
connector_decode_payload (const GByteArray * src)
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
connector_encode_payload (const GByteArray * src)
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

static GByteArray *
connector_msg_to_sysex (const GByteArray * msg)
{
  GByteArray *encoded;
  GByteArray *sysex = g_byte_array_new ();

  g_byte_array_append (sysex, MSG_HEADER, sizeof (MSG_HEADER));
  encoded = connector_encode_payload (msg);
  g_byte_array_append (sysex, encoded->data, encoded->len);
  free_msg (encoded);
  g_byte_array_append (sysex, (guint8 *) "\xf7", 1);

  return sysex;
}

static void
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
	  *id = be32toh (*((uint32_t *) & info_msg->data[6]));
	}
      if (size)
	{
	  *size = be32toh (*((uint32_t *) & info_msg->data[10]));
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
connector_new_msg_uint8 (const guint8 * data, guint len, guint8 type)
{
  GByteArray *msg = connector_new_msg_data (data, len);

  g_byte_array_append (msg, &type, 1);

  return msg;
}

static GByteArray *
connector_new_msg_path (const guint8 * data, guint len, const gchar * path)
{
  GByteArray *msg = connector_new_msg_data (data, len);

  g_byte_array_append (msg, (guchar *) path, strlen (path) + 1);

  return msg;
}

static GByteArray *
connector_new_msg_dir_list (const gchar * path)
{
  return connector_new_msg_path (FS_SAMPLE_READ_DIR_REQUEST,
				 sizeof (FS_SAMPLE_READ_DIR_REQUEST), path);
}

static GByteArray *
connector_new_msg_open_file_read (const gchar * path)
{
  return connector_new_msg_path (FS_SAMPLE_OPEN_FILE_READER_REQUEST,
				 sizeof (FS_SAMPLE_OPEN_FILE_READER_REQUEST),
				 path);
}

static GByteArray *
connector_new_msg_close_file_read (gint id)
{
  uint32_t aux32;
  GByteArray *msg =
    connector_new_msg_data (FS_SAMPLE_CLOSE_FILE_READER_REQUEST,
			    sizeof (FS_SAMPLE_CLOSE_FILE_READER_REQUEST));

  aux32 = htobe32 (id);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (uint32_t));
  return msg;
}

static GByteArray *
connector_new_msg_open_file_write (const gchar * path, guint frames)
{
  uint32_t aux32;
  GByteArray *msg =
    connector_new_msg_path (FS_SAMPLE_OPEN_FILE_WRITER_REQUEST,
			    sizeof (FS_SAMPLE_OPEN_FILE_WRITER_REQUEST),
			    path);

  aux32 = htobe32 ((frames + 32) * 2);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_data_list (const gchar * path, int32_t start_index,
			     int32_t end_index, gboolean all)
{
  uint32_t aux32;
  uint8_t aux8;
  GByteArray *msg = connector_new_msg_path (DATA_LIST_REQUEST,
					    sizeof (DATA_LIST_REQUEST),
					    path);

  aux32 = htobe32 (start_index);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (uint32_t));
  aux32 = htobe32 (end_index);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (uint32_t));
  aux8 = all;
  g_byte_array_append (msg, (guchar *) & aux8, sizeof (uint8_t));

  return msg;
}

static GByteArray *
connector_new_msg_write_file_blk (guint id,
				  gshort ** data,
				  guint frames, ssize_t * total, guint seq)
{
  uint32_t aux32;
  uint16_t aux16;
  int i, consumed, frames_blck;
  GByteArray *msg;

  msg = connector_new_msg_data (FS_SAMPLE_WRITE_FILE_REQUEST,
				sizeof (FS_SAMPLE_WRITE_FILE_REQUEST));

  aux32 = htobe32 (id);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));
  aux32 = htobe32 (SAMPLE_TRANSF_BLOCK_BYTES * seq);
  memcpy (&msg->data[13], &aux32, sizeof (uint32_t));

  frames_blck = SAMPLE_TRANSF_BLOCK_SHORTS;
  consumed = 0;

  if (seq == 0)
    {
      g_byte_array_append (msg,
			   (guchar *) FS_SAMPLE_WRITE_FILE_REQUEST_PAD_1ST,
			   sizeof (FS_SAMPLE_WRITE_FILE_REQUEST_PAD_1ST));

      aux32 = htobe32 (frames * sizeof (gshort));
      memcpy (&msg->data[21], &aux32, sizeof (uint32_t));
      aux32 = htobe32 (frames - 1);
      memcpy (&msg->data[33], &aux32, sizeof (uint32_t));

      consumed = sizeof (FS_SAMPLE_WRITE_FILE_REQUEST_PAD_1ST) / 2;	//consumed is measured in shorts
    }

  frames_blck -= consumed;

  i = 0;
  while (i < frames_blck && *total < frames)
    {
      aux16 = htobe16 (**data);
      g_byte_array_append (msg, (guchar *) & aux16, sizeof (uint16_t));
      (*data)++;
      (*total)++;
      consumed++;
      i++;
    }

  aux32 = htobe32 (consumed * 2);
  memcpy (&msg->data[9], &aux32, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_close_file_write (guint id, guint frames)
{
  uint32_t aux32;
  GByteArray *msg =
    connector_new_msg_data (FS_SAMPLE_CLOSE_FILE_WRITER_REQUEST,
			    sizeof (FS_SAMPLE_CLOSE_FILE_WRITER_REQUEST));

  aux32 = htobe32 (id);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));
  aux32 = htobe32 ((frames + 32) * 2);
  memcpy (&msg->data[9], &aux32, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_read_file_blk (guint id, guint start, guint size)
{
  uint32_t aux;
  GByteArray *msg = connector_new_msg_data (FS_SAMPLE_READ_FILE_REQUEST,
					    sizeof
					    (FS_SAMPLE_READ_FILE_REQUEST));

  aux = htobe32 (id);
  memcpy (&msg->data[5], &aux, sizeof (uint32_t));
  aux = htobe32 (size);
  memcpy (&msg->data[9], &aux, sizeof (uint32_t));
  aux = htobe32 (start);
  memcpy (&msg->data[13], &aux, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_sysex_to_msg (GByteArray * sysex)
{
  GByteArray *msg;
  GByteArray *payload;
  gint len = sysex->len - sizeof (MSG_HEADER) - 1;

  if (len > 0)
    {
      payload = g_byte_array_new ();
      g_byte_array_append (payload, &sysex->data[sizeof (MSG_HEADER)], len);
      msg = connector_decode_payload (payload);
      free_msg (payload);
    }
  else
    {
      msg = NULL;
    }

  return msg;
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
	  errno = EIO;
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
  struct connector_sysex_transfer transfer;
  gchar *text;

  aux = htobe16 (connector->seq);
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
  sysex = connector_msg_to_sysex (msg);

  ret = connector_tx_sysex (connector, sysex, &transfer);

  if (ret >= 0)
    {
      if (debug_level > 1)
	{
	  text = debug_get_hex_msg (sysex);
	  debug_print (2, "Raw message sent (%d): %s\n", sysex->len, text);
	  free (text);
	}

      text = debug_get_hex_msg (msg);
      debug_print (1, "Message sent (%d): %s\n", msg->len, text);
      free (text);
    }

  free_msg (sysex);
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
      debug_print (2, "Buffer content (%zu): %s\n", rx_len, text);
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
	      errno = ENODATA;
	      goto error;
	    }

	  if (connector->rx_len < 0)
	    {
	      errno = EIO;
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
	      errno = EIO;
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

  msg = connector_sysex_to_msg (sysex);
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

struct item_iterator *
connector_read_samples (struct connector *connector, const gchar * dir)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gchar *dir_cp1252 = g_convert (dir, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!dir_cp1252)
    {
      errno = EINVAL;
      return NULL;
    }

  tx_msg = connector_new_msg_dir_list (dir_cp1252);
  g_free (dir_cp1252);

  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      return NULL;
    }

  if (rx_msg->len == 5
      && connector_get_path_type (connector, dir) != ELEKTROID_DIR)
    {
      free_msg (rx_msg);
      errno = ENOTDIR;
      return NULL;
    }

  return connector_new_sample_iterator (rx_msg);
}

static enum item_type
connector_get_path_type (struct connector *connector, const gchar * path)
{
  gchar *name_copy;
  gchar *parent_copy;
  gchar *name;
  gchar *parent;
  enum item_type res;
  struct item_iterator *iterator;

  if (strcmp (path, "/") == 0)
    {
      return ELEKTROID_DIR;
    }

  name_copy = strdup (path);
  parent_copy = strdup (path);
  name = basename (name_copy);
  parent = dirname (parent_copy);
  iterator = connector_read_samples (connector, parent);
  res = ELEKTROID_NONE;
  if (iterator)
    {
      while (!next_item_iterator (iterator))
	{
	  if (strcmp (name, iterator->entry) == 0)
	    {
	      res = iterator->type;
	      break;
	    }
	}
      free_item_iterator (iterator);
    }

  g_free (name_copy);
  g_free (parent_copy);
  return res;
}

static gint
connector_src_dst_common (struct connector *connector,
			  const gchar * src, const gchar * dst,
			  const guint8 * data, guint len)
{
  gint res;
  GByteArray *rx_msg;
  GByteArray *tx_msg = connector_new_msg_data (data, len);

  gchar *dst_cp1252 = g_convert (dst, -1, "CP1252", "UTF8", NULL, NULL, NULL);
  if (!dst_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  gchar *src_cp1252 = g_convert (src, -1, "CP1252", "UTF8", NULL, NULL, NULL);
  if (!src_cp1252)
    {
      g_free (dst_cp1252);
      errno = EINVAL;
      return -1;
    }

  g_byte_array_append (tx_msg, (guchar *) src_cp1252,
		       strlen (src_cp1252) + 1);
  g_byte_array_append (tx_msg, (guchar *) dst_cp1252,
		       strlen (dst_cp1252) + 1);

  g_free (src_cp1252);
  g_free (dst_cp1252);

  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
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
      error_print ("%s (%s)\n", g_strerror (errno),
		   connector_get_msg_string (rx_msg));
    }
  free_msg (rx_msg);

  return res;
}

static gint
connector_rename_sample_file (struct connector *connector, const gchar * src,
			      const gchar * dst)
{
  return connector_src_dst_common (connector, src, dst,
				   FS_SAMPLE_RENAME_FILE_REQUEST,
				   sizeof (FS_SAMPLE_RENAME_FILE_REQUEST));
}

gint
connector_move_samples_item (struct connector *connector, const gchar * src,
			     const gchar * dst)
{
  enum item_type type;
  gint res;
  gchar *src_plus;
  gchar *dst_plus;
  struct item_iterator *iterator;

  debug_print (1, "Renaming remotely from %s to %s...\n", src, dst);

  //Renaming is not implemented for directories so we need to implement it.
  type = connector_get_path_type (connector, src);

  if (type == ELEKTROID_FILE)
    {
      return connector_rename_sample_file (connector, src, dst);
    }
  else if (type == ELEKTROID_DIR)
    {
      res = connector_create_samples_dir (connector, dst);
      if (res)
	{
	  return res;
	}
      iterator = connector_read_samples (connector, src);
      if (iterator)
	{
	  while (!next_item_iterator (iterator) && !res)
	    {
	      src_plus = chain_path (src, iterator->entry);
	      dst_plus = chain_path (dst, iterator->entry);
	      res =
		connector_move_samples_item (connector, src_plus, dst_plus);
	      free (src_plus);
	      free (dst_plus);
	    }
	  free_item_iterator (iterator);
	}
      if (!res)
	{
	  res = connector_delete_samples_dir (connector, src);
	}
      return res;
    }
  else
    {
      errno = EBADF;
      return -1;
    }
}

static gint
connector_path_common (struct connector *connector, const gchar * path,
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
      error_print ("%s (%s)\n", g_strerror (errno),
		   connector_get_msg_string (rx_msg));
    }
  free_msg (rx_msg);

  return res;
}

gint
connector_delete_sample (struct connector *connector, const gchar * path)
{
  return connector_path_common (connector, path,
				FS_SAMPLE_DELETE_FILE_REQUEST,
				sizeof (FS_SAMPLE_DELETE_FILE_REQUEST));
}

gint
connector_delete_samples_dir (struct connector *connector, const gchar * path)
{
  return connector_path_common (connector, path, FS_SAMPLE_DELETE_DIR_REQUEST,
				sizeof (FS_SAMPLE_DELETE_DIR_REQUEST));
}

gint
connector_delete_samples_item (struct connector *connector,
			       const gchar * path)
{
  if (connector_get_path_type (connector, path) == ELEKTROID_DIR)
    {
      return connector_delete_samples_dir (connector, path);
    }
  else
    {
      return connector_delete_sample (connector, path);
    }
}

ssize_t
connector_upload_sample (struct connector *connector, GArray * sample,
			 gchar * path,
			 struct connector_sample_transfer *transfer,
			 void (*progress) (gdouble))
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  ssize_t transferred;
  gshort *data;
  gint id;
  int i;
  gboolean active;
  gchar *path_cp1252 =
    g_convert (path, -1, "CP1252", "UTF8", NULL, NULL, NULL);

  if (!path_cp1252)
    {
      errno = EINVAL;
      return -1;
    }

  //TODO: check if the file already exists? (Device makes no difference between creating a new file and creating an already existent file. The new file would be deleted if an upload is not sent, though.)
  //TODO: limit sample upload?

  tx_msg = connector_new_msg_open_file_write (path_cp1252, sample->len);
  g_free (path_cp1252);
  rx_msg = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg)
    {
      return -1;
    }

  //Response: x, x, x, x, 0xc0, [0 (error), 1 (success)], id, frames
  connector_get_sample_info_from_msg (rx_msg, &id, NULL);
  if (id < 0)
    {
      errno = EEXIST;
      error_print ("%s (%s)\n", g_strerror (errno),
		   connector_get_msg_string (rx_msg));
    }
  free_msg (rx_msg);

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
	connector_new_msg_write_file_blk (id, &data, sample->len,
					  &transferred, i);
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

      tx_msg = connector_new_msg_close_file_write (id, transferred);
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
connector_download_sample (struct connector *connector, const gchar * path,
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

  tx_msg = connector_new_msg_open_file_read (path_cp1252);
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
	SAMPLE_TRANSF_BLOCK_BYTES ? SAMPLE_TRANSF_BLOCK_BYTES : frames -
	next_block_start;
      tx_msg =
	connector_new_msg_read_file_blk (id, next_block_start, req_size);
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
	  v = be16toh (*frame);
	  g_array_append_val (result, v);
	  frame++;
	}
    }
  else
    {
      result = NULL;
    }

  tx_msg = connector_new_msg_close_file_read (id);
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
connector_create_samples_dir (struct connector *connector, const gchar * path)
{
  return connector_path_common (connector, path, FS_SAMPLE_CREATE_DIR_REQUEST,
				sizeof (FS_SAMPLE_CREATE_DIR_REQUEST));
}

static GByteArray *
connector_new_msg_upgrade_os_start (guint size)
{
  GByteArray *msg = connector_new_msg_data (OS_UPGRADE_START_REQUEST,
					    sizeof
					    (OS_UPGRADE_START_REQUEST));

  memcpy (&msg->data[5], &size, sizeof (uint32_t));

  return msg;
}

static GByteArray *
connector_new_msg_upgrade_os_write (GByteArray * os_data, gint * offset)
{
  GByteArray *msg = connector_new_msg_data (OS_UPGRADE_WRITE_RESPONSE,
					    sizeof
					    (OS_UPGRADE_WRITE_RESPONSE));
  guint len;
  uint32_t crc;
  uint32_t aux32;

  if (*offset + OS_TRANSF_BLOCK_BYTES < os_data->len)
    {
      len = OS_TRANSF_BLOCK_BYTES;
    }
  else
    {
      len = os_data->len - *offset;
    }

  crc = crc32 (0xffffffff, &os_data->data[*offset], len);

  debug_print (2, "CRC: %0x\n", crc);

  aux32 = htobe32 (crc);
  memcpy (&msg->data[5], &aux32, sizeof (uint32_t));
  aux32 = htobe32 (len);
  memcpy (&msg->data[9], &aux32, sizeof (uint32_t));
  aux32 = htobe32 (*offset);
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
      free_msg (rx_msg);
      goto end;
    }

  free_msg (rx_msg);

  offset = 0;
  while (offset < data->len && transfer->active)
    {
      tx_msg = connector_new_msg_upgrade_os_write (data, &offset);
      rx_msg = connector_tx_and_rx (connector, tx_msg);

      if (!rx_msg)
	{
	  res = -1;
	  break;
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
	  free_msg (rx_msg);
	  break;
	}

      free_msg (rx_msg);

      usleep (REST_TIME);
    }

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

gint
connector_get_storage_stats (struct connector *connector,
			     enum connector_storage type,
			     struct connector_storage_stats *statfs)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint8 op;
  uint64_t *data;
  int index;
  gint res = 0;

  tx_msg = connector_new_msg_uint8 (STORAGEINFO_REQUEST,
				    sizeof (STORAGEINFO_REQUEST), type);
  rx_msg = connector_tx_and_rx (connector, tx_msg);

  if (!rx_msg)
    {
      return -1;
    }

  op = connector_get_msg_status (rx_msg);
  if (!op)
    {
      errno = EIO;
      error_print ("%s (%s)\n", g_strerror (errno),
		   connector_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return -1;
    }

  index = 0;
  for (int i = 0, storage = STORAGE_PLUS_DRIVE; storage <= STORAGE_RAM;
       i++, storage <<= 1)
    {
      if (storage == type)
	{
	  index = i;
	}
    }

  statfs->name = FS_TYPE_NAMES[index];
  data = (uint64_t *) & rx_msg->data[6];
  statfs->bfree = be64toh (*data);
  data = (uint64_t *) & rx_msg->data[14];
  statfs->bsize = be64toh (*data);

  free_msg (rx_msg);

  return res;
}

float
connector_get_storage_stats_percent (struct connector_storage_stats *statfs)
{
  return (statfs->bsize - statfs->bfree) * 100.0 / statfs->bsize;
}

static const struct connector_device_desc *
connector_get_device_desc (guint8 id)
{
  guint total =
    sizeof (CONNECTOR_DEVICE_DESCS) / sizeof (struct connector_device_desc *);
  guint i;

  for (i = 0; i < total; i++)
    {
      if (id == CONNECTOR_DEVICE_DESCS[i]->id)
	{
	  return CONNECTOR_DEVICE_DESCS[i];
	}
    }
  return &NULL_DEVICE_DESC;
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
      err = -EINVAL;
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

  tx_msg = connector_new_msg_data (PING_REQUEST, sizeof (PING_REQUEST));
  rx_msg_device = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg_device)
    {
      err = -errno;
      goto cleanup;
    }

  tx_msg =
    connector_new_msg_data (SOFTWARE_VERSION_REQUEST,
			    sizeof (SOFTWARE_VERSION_REQUEST));
  rx_msg_fw_ver = connector_tx_and_rx (connector, tx_msg);
  if (!rx_msg_fw_ver)
    {
      err = -errno;
      goto cleanup_device;
    }

  if (debug_level > 1)
    {
      tx_msg =
	connector_new_msg_data (DEVICEUID_REQUEST,
				sizeof (DEVICEUID_REQUEST));
      rx_msg_uid = connector_tx_and_rx (connector, tx_msg);
      if (rx_msg_uid)
	{
	  debug_print (1, "UID: %x\n", *((guint32 *) & rx_msg_uid->data[5]));
	  free_msg (rx_msg_uid);
	}
    }

  connector->device_desc = connector_get_device_desc (rx_msg_device->data[5]);

  snprintf (connector->device_name, LABEL_MAX, "%s %s (%s)",
	    connector->device_desc->model,
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

static struct connector_system_device *
connector_get_system_device (snd_ctl_t * ctl, int card, int device)
{
  snd_rawmidi_info_t *info;
  const gchar *name;
  const gchar *sub_name;
  int subs, subs_in, subs_out;
  int sub;
  int err;
  struct connector_system_device *connector_system_device;

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
      connector_system_device =
	malloc (sizeof (struct connector_system_device));
      connector_system_device->card = card;
      connector_system_device->name = strdup (sub_name);
      return connector_system_device;
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
  struct connector_system_device *connector_system_device;

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
      connector_system_device =
	connector_get_system_device (ctl, card, device);
      if (connector_system_device)
	{
	  g_array_append_vals (devices, connector_system_device, 1);
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
connector_get_system_devices ()
{
  gint card, err;
  GArray *devices =
    g_array_new (FALSE, FALSE, sizeof (struct connector_system_device));

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

static guint
connector_next_data_entry (struct item_iterator *iter)
{
  gchar *entry_cp1252;
  guint32 *data32;
  guint16 *data16;
  guint8 type;
  guint8 has_children;
  struct connector_iterator_data *data = iter->data;

  if (iter->entry != NULL)
    {
      g_free (iter->entry);
    }

  if (data->pos == data->msg->len)
    {
      iter->entry = NULL;
      return -ENOENT;
    }

  entry_cp1252 = (gchar *) & data->msg->data[data->pos];
  iter->entry =
    g_convert (entry_cp1252, -1, "UTF8", "CP1252", NULL, NULL, NULL);
  data->pos += strlen (entry_cp1252) + 1;
  has_children = data->msg->data[data->pos];
  data->pos++;
  type = data->msg->data[data->pos];
  data->pos++;

  switch (type)
    {
    case 1:
      iter->type = ELEKTROID_DIR;
      data->pos += sizeof (guint32);	// child entries
      iter->size = 0;
      break;
    case 2:
      iter->type = has_children ? ELEKTROID_DIR : ELEKTROID_FILE;

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->id = be32toh (*data32);	//index
      data->pos += sizeof (gint32);

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->size = be32toh (*data32);
      data->pos += sizeof (guint32);

      data16 = (guint16 *) & data->msg->data[data->pos];
      data->operations = be16toh (*data16);
      data->pos += sizeof (guint16);

      data->has_valid_data = data->msg->data[data->pos];
      data->pos++;

      data->has_metadata = data->msg->data[data->pos];
      data->pos++;

      break;
    default:
      error_print ("Unrecognized data entry: %d\n", iter->type);
      break;
    }

  return 0;
}

static struct item_iterator *
connector_new_data_iterator (GByteArray * msg)
{
  struct item_iterator *iter = malloc (sizeof (struct item_iterator));
  struct connector_iterator_data *data =
    malloc (sizeof (struct connector_iterator_data));

  data->msg = msg;
  data->pos = 18;

  iter->data = data;
  iter->entry = NULL;
  iter->next = connector_next_data_entry;
  iter->free = connector_free_iterator_data;

  return iter;
}

struct item_iterator *
connector_read_data (struct connector *connector, const gchar * path)
{
  int res;
  GByteArray *tx_msg;
  GByteArray *rx_msg;

  tx_msg = connector_new_msg_data_list (path, 0, 0, 1);
  rx_msg = connector_tx_and_rx (connector, tx_msg);

  if (!rx_msg)
    {
      return NULL;
    }

  res = connector_get_msg_status (rx_msg);
  if (!res)
    {
      error_print ("Cannot get data list: %s\n",
		   connector_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return NULL;
    }

  return connector_new_data_iterator (rx_msg);
}

gint
connector_move_data_item (struct connector *connector, const gchar * src,
			  const gchar * dst)
{
  return connector_src_dst_common (connector, src, dst, DATA_MOVE_REQUEST,
				   sizeof (DATA_MOVE_REQUEST));
}

gint
connector_copy_data_item (struct connector *connector, const gchar * src,
			  const gchar * dst)
{
  return connector_src_dst_common (connector, src, dst, DATA_COPY_REQUEST,
				   sizeof (DATA_COPY_REQUEST));
}

gint
connector_clear_data_item (struct connector *connector, const gchar * path)
{
  return connector_path_common (connector, path, DATA_CLEAR_REQUEST,
				sizeof (DATA_CLEAR_REQUEST));
}

gint
connector_swap_data_item (struct connector *connector, const gchar * src,
			  const gchar * dst)
{
  return connector_src_dst_common (connector, src, dst, DATA_SWAP_REQUEST,
				   sizeof (DATA_SWAP_REQUEST));
}
