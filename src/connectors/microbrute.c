/*
 *   microbrute.c
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

#include "microbrute.h"
#include "common.h"

#define MICROBRUTE_MAX_SEQ_STR_LEN 256

#define MICROBRUTE_MAX_SEQUENCES 8
#define MICROBRUTE_SEQUENCE_PREFIX "MicroBrute sequence"

#define MICROBRUTE_SEQUENCE_REQUEST_SEQ_POS 6
#define MICROBRUTE_SEQUENCE_REQUEST_ID_POS 9
#define MICROBRUTE_SEQUENCE_REQUEST_OFFSET_POS 10
#define MICROBRUTE_SEQUENCE_RESPONSE_LEN_POS 11
#define MICROBRUTE_SEQUENCE_RESPONSE_DATA_POS 12
#define MICROBRUTE_SEQUENCE_TXT_POS 2

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 FAMILY_ID[] = { 0x4, 0x0 };
static const guint8 MODEL_ID[] = { 0x2, 0x1 };

static const guint8 MICROBRUTE_SEQUENCE_REQUEST[] =
  { 0xf0, 0x0, 0x20, 0x6B, 0x5, 0x1, 0x0, 0x03, 0x3B, 0x0, 0x0, 0x20, 0xf7 };

static const guint8 MICROBRUTE_SEQUENCE_MSG[] =
  { 0xf0, 0x0, 0x20, 0x6b, 0x05, 0x01, 0x0, 0x23, 0x3a, 0x0, 0x0, 0x20,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0xf7
};

enum cz_fs
{
  FS_MICROBRUTE_SEQUENCE = 1
};

static guint8
microbrute_get_seq (struct backend *backend)
{
  guint8 *seq = backend->data;
  guint8 value = *seq;
  (*seq)++;
  if (*seq == 0x80)
    {
      *seq = 0;
    }
  return value;
}

static gchar *
microbrute_get_download_path (struct backend *backend,
			      const struct fs_operations *ops,
			      const gchar * dst_dir, const gchar * src_path)
{
  gchar *name = malloc (PATH_MAX);
  gchar *src_path_copy = strdup (src_path);
  gchar *filename = basename (src_path_copy);
  gint index = atoi (filename);
  snprintf (name, PATH_MAX, "%s/%s %d.mbseq", dst_dir,
	    MICROBRUTE_SEQUENCE_PREFIX, index + 1);
  g_free (src_path_copy);

  return name;
}

static guint
microbrute_next_dentry (struct item_iterator *iter)
{
  guint *next = iter->data;

  if (*next >= MICROBRUTE_MAX_SEQUENCES)
    {
      return -ENOENT;
    }

  iter->item.id = *next;
  snprintf (iter->item.name, LABEL_MAX, "%d", *next + 1);
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  (*next)++;

  return 0;
}

static gint
microbrute_read_dir (struct backend *backend, struct item_iterator *iter,
		     const gchar * path)
{
  guint *next;

  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  next = g_malloc (sizeof (guint));
  *next = 0;
  iter->data = next;
  iter->next = microbrute_next_dentry;
  iter->free = g_free;

  return 0;
}

static GByteArray *
microbrute_get_sequence_request_msg (struct backend *backend, guint8 id,
				     guint8 offset)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (MICROBRUTE_SEQUENCE_REQUEST));
  g_byte_array_append (tx_msg, MICROBRUTE_SEQUENCE_REQUEST,
		       sizeof (MICROBRUTE_SEQUENCE_REQUEST));
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_SEQ_POS] =
    microbrute_get_seq (backend);
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_ID_POS] = id;
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_OFFSET_POS] = offset;
  return tx_msg;
}

static gint
microbrute_download_seq_data (struct backend *backend, guint seqnum,
			      guint offset, gchar * sequence)
{
  GByteArray *tx_msg, *rx_msg;
  gchar aux[LABEL_MAX];
  gboolean first;
  guint8 *step;

  tx_msg = microbrute_get_sequence_request_msg (backend, seqnum, offset);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  first = offset ? FALSE : TRUE;
  step = &rx_msg->data[MICROBRUTE_SEQUENCE_RESPONSE_DATA_POS];
  while (*step && *step != 0xf7)
    {
      snprintf (aux, LABEL_MAX, "%s", first ? "" : " ");
      strcat (sequence, aux);
      first = FALSE;
      if (*step == 0x7f)
	{
	  strcat (sequence, "x");
	}
      else
	{
	  snprintf (aux, LABEL_MAX, "%02d", *step);
	  strcat (sequence, aux);
	}
      step++;
    }

  free_msg (rx_msg);

  return 0;
}

static gint
microbrute_download (struct backend *backend, const gchar * src_path,
		     GByteArray * output, struct job_control *control)
{
  gchar sequence[MICROBRUTE_MAX_SEQ_STR_LEN];
  gboolean active;
  gchar *src_path_copy = strdup (src_path);
  gchar *filename = basename (src_path_copy);
  guint seqnum = atoi (filename);
  gint err;

  g_free (src_path_copy);

  snprintf (sequence, MICROBRUTE_MAX_SEQ_STR_LEN, "%1d:", seqnum + 1);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  err = microbrute_download_seq_data (backend, seqnum, 0, sequence);
  if (err)
    {
      return err;
    }

  set_job_control_progress (control, 0.5);

  err = microbrute_download_seq_data (backend, seqnum, 0x20, sequence);
  if (err)
    {
      return err;
    }

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);
  if (active)
    {
      set_job_control_progress (control, 1.0);
    }
  else
    {
      return -ECANCELED;
    }

  g_byte_array_append (output, (guint8 *) sequence, strlen (sequence));

  return 0;
}

static GByteArray *
microbrute_set_sequence_request_msg (struct backend *backend, guint8 id,
				     guint8 offset)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (MICROBRUTE_SEQUENCE_MSG));
  g_byte_array_append (tx_msg, MICROBRUTE_SEQUENCE_MSG,
		       sizeof (MICROBRUTE_SEQUENCE_MSG));
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_SEQ_POS] =
    microbrute_get_seq (backend);
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_ID_POS] = id;
  tx_msg->data[MICROBRUTE_SEQUENCE_REQUEST_OFFSET_POS] = offset;
  return tx_msg;
}

static gint
microbrute_send_seq_msg (struct backend *backend, guint8 seqnum,
			 guint8 offset, gchar ** tokens, gint * pos,
			 gint total)
{
  struct sysex_transfer transfer;
  guint8 steps = 0, id = seqnum;
  gchar *token = *tokens;
  gint err;
  guint8 *step;

  transfer.raw = microbrute_set_sequence_request_msg (backend, id, offset);

  step = &transfer.raw->data[MICROBRUTE_SEQUENCE_RESPONSE_DATA_POS];
  while (steps < 32 && *pos < total)
    {
      if (*token < 0x20)
	{
	  error_print ("Invalid character\n");
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (*token == ' ')
	{
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (token[0] == '0' && token[1] != ' ')
	{
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (*token == 'x' || *token == 'X')
	{
	  *step = 0x7f;
	  token++;
	  (*pos)++;
	  debug_print (2, "Note: -\n");
	}
      else
	{
	  gchar *rem;
	  glong note = strtol (token, &rem, 10);
	  *step = note >= 0x7f ? 0x7f : note;
	  *step = *step < 12 ? 0x7f : *step;
	  if (*step == 0 && token == rem)
	    {
	      error_print ("Error while reading note\n");
	      token++;
	      (*pos)++;
	      continue;
	    }
	  token = rem;
	  *pos += (*step >= 100) ? 3 : (*step >= 10) ? 2 : 1;
	  debug_print (2, "Note: 0x%02x (%d)\n", *step, *step);
	}
      steps++;
      step++;
    }
  transfer.raw->data[MICROBRUTE_SEQUENCE_RESPONSE_LEN_POS] = steps;

  //This doesn't need synchronized access as the caller provices this already.
  err = backend_tx_sysex (backend, &transfer);
  free_msg (transfer.raw);

  *tokens = token;

  return err < 0 ? err : steps;
}

static gint
microbrute_upload (struct backend *backend, const gchar * path,
		   GByteArray * input, struct job_control *control)
{
  gchar *token = (gchar *) & input->data[MICROBRUTE_SEQUENCE_TXT_POS];
  gint pos = MICROBRUTE_SEQUENCE_TXT_POS;
  guint seqnum;
  gint steps;

  if (common_slot_get_id_name_from_path (path, &seqnum, NULL))
    {
      return -EBADSLT;
    }

  g_mutex_lock (&backend->mutex);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  steps = microbrute_send_seq_msg (backend, seqnum, 0, &token, &pos,
				   input->len);
  if (steps < 0)
    {
      goto end;
    }
  else if (pos < input->len)
    {
      set_job_control_progress (control, 0.5);
      steps = microbrute_send_seq_msg (backend, seqnum, 0x20, &token, &pos,
				       input->len);
      if (steps < 0)
	{
	  goto end;
	}
    }

  set_job_control_progress (control, 1.0);

end:
  g_mutex_unlock (&backend->mutex);
  return steps < 0 ? steps : 0;
}

static const struct fs_operations FS_MICROBRUTE_OPERATIONS = {
  .fs = FS_MICROBRUTE_SEQUENCE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_NAME,
  .name = "sequence",
  .gui_name = "Sequences",
  .gui_icon = BE_FILE_ICON_SEQ,
  .type_ext = "mbseq",
  .readdir = microbrute_read_dir,
  .print_item = common_print_item,
  .download = microbrute_download,
  .upload = microbrute_upload,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microbrute_get_download_path
};

static const struct fs_operations *FS_MICROBRUTE_OPERATIONS_LIST[] = {
  &FS_MICROBRUTE_OPERATIONS, NULL
};

gint
microbrute_handshake (struct backend *backend)
{
  guint8 *seq;

  if (memcmp (backend->midi_info.company, ARTURIA_ID, sizeof (ARTURIA_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  seq = malloc (sizeof (guint8));
  *seq = 0;

  backend->device_desc.filesystems = FS_MICROBRUTE_SEQUENCE;
  backend->fs_ops = FS_MICROBRUTE_OPERATIONS_LIST;
  backend->destroy_data = backend_destroy_data;
  backend->data = seq;

  snprintf (backend->device_name, LABEL_MAX, "Arturia MicroBrute %d.%d.%d.%d",
	    backend->midi_info.version[0], backend->midi_info.version[1],
	    backend->midi_info.version[2], backend->midi_info.version[3]);

  return 0;
}
