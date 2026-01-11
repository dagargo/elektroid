/*
 *   volca_sample_2.c
 *   Copyright (C) 2025 David García Goñi <dagargo@gmail.com>
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
#include "common.h"
#include "sample.h"

#define VOLCA_SAMPLE_2_RATE 31250

#define VOLCA_SAMPLE_2_BYTES_PER_SECTOR (4 * KI)
#define VOLCA_SAMPLE_2_REST_TIME_US 5000

#define VOLCA_SAMPLE_2_SAMPLE_MAX 200
#define VOLCA_SAMPLE_2_SAMPLE_NAME_LEN 24

#define VOLCA_SAMPLE_2_PATTERN_MAX 16
#define VOLCA_SAMPLE_2_PATTERN_NAME_LEN 32
#define VOLCA_SAMPLE_2_PATTERN_NAME_POS 16

#define VOLCA_SAMPLE_2_SAMPLE_START_POINT 0.91

#define VOLCA_SAMPLE_2_GET_MSG_OP(msg) (msg->data[6])

static const guint8 FAMILY_ID[] = { 0x2d, 0x01 };
static const guint8 MODEL_ID[] = { 0x8, 0x0 };

static const guint8 MSG_HEADER[] = { 0xf0, 0x42, 0x30, 0, 1, 0x2d };

// KORG librarian uses zip files with the `vlcsplpatt` extension for the patterns, which contain these files.
// Prog_000.prog_bin, which is the actual pattern,
// Prog_000.prog_info, which is an XML file, and
// FileInformation.xml.
// Other extension is used here to clarify that our patterns are not binary compatible with the librarian's.
// However, notice that a `vlcsplpattb` file is exactly the same as the prog_bin file above.
static const gchar *VOLCA_SAMPLE_2_PATTERN_EXTS[] = { "vlcsplpattb", NULL };

enum volca_sample_2_fs
{
  FS_VOLCA_SAMPLE_2_SAMPLE,
  FS_VOLCA_SAMPLE_2_SLICE,	//Same as sample but with the sample size tweaked so that length reaches 100 % of the efective size
  FS_VOLCA_SAMPLE_2_PATTERN
};

struct volca_sample_2_iter_data
{
  guint next;
  struct backend *backend;
};

struct volca_sample_2_sample_header
{
  gchar name[VOLCA_SAMPLE_2_SAMPLE_NAME_LEN];
  guint32 frames;
  guint16 level;
  guint16 speed;
};

static GByteArray *
volca_sample_2_get_msg (guint8 op, const guint8 *data, guint len)
{
  GByteArray *msg = g_byte_array_sized_new (sizeof (MSG_HEADER) + 2 + len);
  g_byte_array_append (msg, MSG_HEADER, sizeof (MSG_HEADER));
  g_byte_array_append (msg, &op, 1);
  g_byte_array_append (msg, data, len);
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);
  return msg;
}

static void
volca_sample_2_set_sample_id (guint8 *buffer, guint id)
{
  buffer[0] = id & 0x7f;
  buffer[1] = id >> 7;
}

static gint
volca_sample_2_sample_get_header (struct backend *backend, guint id,
				  struct volca_sample_2_sample_header *header)
{
  guint8 payload[2];
  GByteArray *tx_msg, *rx_msg;

  volca_sample_2_set_sample_id (payload, id);
  tx_msg = volca_sample_2_get_msg (0x1e, payload, 2);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (VOLCA_SAMPLE_2_GET_MSG_OP (rx_msg) != 0x4e)
    {
      free_msg (rx_msg);
      return -EIO;
    }

  common_midi_msg_to_8bit_msg (&rx_msg->data[9], (guint8 *) header, 37);

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_data (debug_level, (guint8 *) header,
					sizeof (struct
						volca_sample_2_sample_header));
      debug_print (1, "Message received (%zu): %s",
		   sizeof (struct volca_sample_2_sample_header), text);
      g_free (text);
    }

  free_msg (rx_msg);

  usleep (VOLCA_SAMPLE_2_REST_TIME_US);

  return 0;
}

static GByteArray *
volca_sample_2_sample_get_data (struct backend *backend, guint id,
				guint frames)
{
  guint size, dump_size;
  GByteArray *data;
  guint8 payload[2];
  GByteArray *tx_msg, *rx_msg;

  volca_sample_2_set_sample_id (payload, id);
  tx_msg = volca_sample_2_get_msg (0x1f, payload, 2);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return NULL;
    }
  if (VOLCA_SAMPLE_2_GET_MSG_OP (rx_msg) != 0x4f)
    {
      free_msg (rx_msg);
      return NULL;
    }

  size = frames * sizeof (gint16);
  dump_size = common_8bit_msg_to_midi_msg_size (size);
  if (rx_msg->len != dump_size + 10)
    {
      free_msg (rx_msg);
      return NULL;
    }

  data = g_byte_array_sized_new (size);
  data->len = size;
  common_midi_msg_to_8bit_msg (&rx_msg->data[9], data->data, dump_size);
  free_msg (rx_msg);

  usleep (VOLCA_SAMPLE_2_REST_TIME_US);

  return data;
}

static gint
volca_sample_2_sample_next_dentry (struct item_iterator *iter)
{
  gint err;
  struct volca_sample_2_sample_header header;
  struct volca_sample_2_iter_data *data = iter->data;

  if (data->next >= VOLCA_SAMPLE_2_SAMPLE_MAX)
    {
      return -ENOENT;
    }

  err = volca_sample_2_sample_get_header (data->backend, data->next, &header);
  if (err)
    {
      return err;
    }

  item_set_name (&iter->item, "%.*s", VOLCA_SAMPLE_2_SAMPLE_NAME_LEN,
		 header.name);
  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = GUINT32_FROM_LE (header.frames) * sizeof (gint16);
  sample_info_init (&iter->item.sample_info, FALSE);

  (data->next)++;

  return 0;
}

static gint
volca_sample_2_sample_read_dir (struct backend *backend,
				struct item_iterator *iter, const gchar *dir,
				const gchar **extensions)
{
  struct volca_sample_2_iter_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct volca_sample_2_iter_data));
  data->next = 0;
  data->backend = backend;

  item_iterator_init (iter, dir, data, volca_sample_2_sample_next_dentry,
		      g_free);

  return 0;
}

static struct sample_info *
volca_sample_2_sample_info_init (guint32 frames)
{
  struct sample_info *sample_info = sample_info_new (FALSE);
  sample_info->loop_type = 0x7f;
  sample_info->channels = 1;
  sample_info->rate = VOLCA_SAMPLE_2_RATE;
  sample_info->format = SF_FORMAT_PCM_16;
  sample_info->frames = frames;
  sample_info->loop_start = sample_info->frames - 1;
  sample_info->loop_end = sample_info->loop_start;
  return sample_info;
}

static gint
volca_sample_2_sample_download (struct backend *backend, const gchar *path,
				struct idata *sample,
				struct task_control *control)
{
  gint err;
  guint id;
  GByteArray *data;
  gchar name[VOLCA_SAMPLE_2_SAMPLE_NAME_LEN + 1];
  struct sample_info *sample_info;
  struct volca_sample_2_sample_header header;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (id >= VOLCA_SAMPLE_2_SAMPLE_MAX)
    {
      return -EINVAL;
    }

  task_control_reset (control, 2);

  err = volca_sample_2_sample_get_header (backend, id, &header);
  if (err)
    {
      return err;
    }

  task_control_set_progress (control, 1.0);
  control->part++;

  data = volca_sample_2_sample_get_data (backend, id, header.frames);
  if (!data)
    {
      return -EIO;
    }

  sample_info = volca_sample_2_sample_info_init (header.frames);
  snprintf (name, VOLCA_SAMPLE_2_SAMPLE_NAME_LEN + 1, "%.*s",
	    VOLCA_SAMPLE_2_SAMPLE_NAME_LEN, header.name);
  idata_init (sample, data, g_strdup (name), sample_info, sample_info_free);

  task_control_set_progress (control, 1.0);
  control->part++;

  return 0;
}

static gint
volca_sample_2_sample_save (const gchar *path, struct idata *sample,
			    struct task_control *control)
{
  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

static gchar *
volca_sample_2_get_sample_id_as_slot (struct item *item,
				      struct backend *backend)
{
  return common_get_id_as_slot_padded (item, backend, 3);
}

static gint
volca_sample_2_sample_load (const gchar *path, struct idata *sample,
			    struct task_control *control)
{
  return common_sample_load (path, sample, control, 1, VOLCA_SAMPLE_2_RATE,
			     SF_FORMAT_PCM_16, FALSE);
}

static gint
volca_sample_2_get_msg_err (GByteArray *msg)
{
  switch (VOLCA_SAMPLE_2_GET_MSG_OP (msg))
    {
    case 0x23:
      return 0;
    case 0x24:
      return -EBUSY;
    case 0x25:
      return -ENOSPC;
    case 0x26:
      return -EBADMSG;
    default:
      return -EIO;
    }
}

//As the device uses a single message, the sample size is limited by its maximum size.
static guint
volca_sample_2_sample_get_max_size_in_msg (guint size)
{
  guint max_size = common_midi_msg_to_8bit_msg_size (BE_MAX_BUFF_SIZE - 10);	//10 bytes used by non sample data in the message

//But max_size needs to be pair as the samples use 2 bytes.
  if (max_size % 2)
    {
      max_size -= 1;
    }

  if (size > max_size)
    {
      size = max_size;
      warn_print ("Sample truncated to %d B due to max message size (%d B)",
		  size, BE_MAX_BUFF_SIZE);
    }

  return size;
}

static gint
volca_sample_2_sample_upload_params (struct backend *backend,
				     const gchar *path, const gchar *name,
				     guint8 *data, guint size, guint16 level,
				     guint16 speed,
				     struct task_control *control)
{
  gint err;
  guint id;
  guint8 *buff;
  guint buff_size;
  guint8 header_dump[39];
  GByteArray *tx_msg, *rx_msg;
  struct volca_sample_2_sample_header header;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (id >= VOLCA_SAMPLE_2_SAMPLE_MAX)
    {
      return -EINVAL;
    }

  if (control)
    {
      task_control_reset (control, 2);
    }

  memset (header.name, 0, VOLCA_SAMPLE_2_SAMPLE_NAME_LEN);
  memcpy (header.name, name, MIN (strlen (name),
				  VOLCA_SAMPLE_2_SAMPLE_NAME_LEN));
  header.frames = size / sizeof (gint16);
  header.level = level;
  header.speed = speed;

  volca_sample_2_set_sample_id (header_dump, id);
  common_8bit_msg_to_midi_msg ((guint8 *) & header, &header_dump[2],
			       sizeof (struct volca_sample_2_sample_header));
  tx_msg = volca_sample_2_get_msg (0x4e, header_dump, 39);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = volca_sample_2_get_msg_err (rx_msg);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  if (control)
    {
      task_control_set_progress (control, 1.0);
      control->part++;
    }

  usleep (VOLCA_SAMPLE_2_REST_TIME_US);

  size = volca_sample_2_sample_get_max_size_in_msg (size);
  buff_size = 2 + common_8bit_msg_to_midi_msg_size (size);
  buff = g_malloc (buff_size);
  volca_sample_2_set_sample_id (buff, id);
  common_8bit_msg_to_midi_msg (data, &buff[2], size);

  tx_msg = volca_sample_2_get_msg (0x4f, buff, buff_size);
  g_free (buff);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = volca_sample_2_get_msg_err (rx_msg);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  if (control)
    {
      task_control_set_progress (control, 1.0);
      control->part++;
    }

  usleep (VOLCA_SAMPLE_2_REST_TIME_US);

  return 0;
}

static gint
volca_sample_2_sample_upload (struct backend *backend, const gchar *path,
			      struct idata *sample,
			      struct task_control *control)
{
  // Level 100 %
  // Speed in center as in the official documentation (16384)
  return volca_sample_2_sample_upload_params (backend, path, sample->name,
					      sample->content->data,
					      sample->content->len, 0xffff,
					      0x4000, control);
}

static gint
volca_sample_2_sample_clear (struct backend *backend, const gchar *path)
{
  return volca_sample_2_sample_upload_params (backend, path, "", NULL, 0, 0,
					      0, NULL);
}

static const struct fs_operations FS_VOLCA_SAMPLE_2_SAMPLE_OPERATIONS = {
  .id = FS_VOLCA_SAMPLE_2_SAMPLE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = volca_sample_2_sample_read_dir,
  .print_item = common_print_item,
  .get_slot = volca_sample_2_get_sample_id_as_slot,
  .delete = volca_sample_2_sample_clear,
  .download = volca_sample_2_sample_download,
  .upload = volca_sample_2_sample_upload,
  .load = volca_sample_2_sample_load,
  .save = volca_sample_2_sample_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

static gint
volca_sample_2_slice_load (const gchar *path, struct idata *sample,
			   struct task_control *control)
{
  guint err;
  guint sample_len, slice_len, sample_size, slice_size;

  err = common_sample_load (path, sample, control, 1, VOLCA_SAMPLE_2_RATE,
			    SF_FORMAT_PCM_16, FALSE);
  if (err)
    {
      return err;
    }

  // Sample start point goes up to roughly 91 % of the sample length.
  // By appending a silence at the end and making the sample 100 / 91 its original size,
  // 91 % of (100 / 91) * 100 % becomes 100 % of the original length.
  sample_size = sample->content->len;
  sample_len = sample_size / sizeof (gint16);
  slice_len = ceil (sample_len / VOLCA_SAMPLE_2_SAMPLE_START_POINT);
  slice_size = slice_len * sizeof (gint16);

  // sample->info can be ignored as is not used when uploading.

  debug_print (1, "Adjusting sample length from %u (%u B) to %u (%u B)...",
	       sample_len, sample_size, slice_len, slice_size);

  g_byte_array_set_size (sample->content, slice_size);
  memset (&sample->content->data[sample_size], 0, slice_size - sample_size);

  return 0;
}

static gint
volca_sample_2_slice_save (const gchar *path, struct idata *sample,
			   struct task_control *control)
{
  struct sample_info *sample_info;
  guint sample_len, slice_len, sample_size, slice_size;

  //See volca_sample_2_slice_load.
  slice_size = sample->content->len;
  slice_len = slice_size / sizeof (gint16);
  sample_len = slice_len * VOLCA_SAMPLE_2_SAMPLE_START_POINT;
  sample_size = sample_len * sizeof (gint16);
  sample->content->len = sample_size;

  sample_info = sample->info;
  sample_info->loop_start = sample_len - 1;
  sample_info->loop_end = sample_info->loop_start;

  debug_print (1, "Adjusting sample length from %u (%u B) to %u (%u B)...",
	       slice_len, slice_size, sample_len, sample_size);

  return sample_save_to_file (path, sample, control,
			      SF_FORMAT_WAV | SF_FORMAT_PCM_16);
}

// The only functional difference with the sample filesystem are volca_sample_2_slice_load and volca_sample_2_slice_save.

static const struct fs_operations FS_VOLCA_SAMPLE_2_SLICE_OPERATIONS = {
  .id = FS_VOLCA_SAMPLE_2_SLICE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "slice",
  .gui_name = "Slices",
  .gui_icon = FS_ICON_SLICE,
  .file_icon = FS_ICON_WAVE,
  .readdir = volca_sample_2_sample_read_dir,
  .print_item = common_print_item,
  .get_slot = volca_sample_2_get_sample_id_as_slot,
  .delete = volca_sample_2_sample_clear,
  .download = volca_sample_2_sample_download,
  .upload = volca_sample_2_sample_upload,
  .load = volca_sample_2_slice_load,
  .save = volca_sample_2_slice_save,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

static gchar *
volca_sample_2_get_pattern_id_as_slot (struct item *item,
				       struct backend *backend)
{
  return common_get_id_as_slot_padded (item, backend, 2);
}

static const gchar **
volca_sample_2_pattern_get_extensions (struct backend *backend,
				       const struct fs_operations *ops)
{
  return VOLCA_SAMPLE_2_PATTERN_EXTS;
}

static gint
volca_sample_2_pattern_download_by_id (struct backend *backend, guint8 id,
				       struct idata *pattern,
				       struct task_control *control)
{
  guint size;
  gchar *pattern_name;
  GByteArray *content;
  gchar name[LABEL_MAX];
  GByteArray *tx_msg, *rx_msg;

  if (control)
    {
      task_control_reset (control, 1);
    }

  id--;				// O based
  if (id >= VOLCA_SAMPLE_2_PATTERN_MAX)
    {
      return -EINVAL;
    }

  tx_msg = volca_sample_2_get_msg (0x1d, &id, 1);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (VOLCA_SAMPLE_2_GET_MSG_OP (rx_msg) != 0x4d)
    {
      free_msg (rx_msg);
      return -EIO;
    }

  size = common_midi_msg_to_8bit_msg_size (rx_msg->len - 9);
  content = g_byte_array_sized_new (size);
  content->len = size;
  common_midi_msg_to_8bit_msg (&rx_msg->data[8], content->data,
			       rx_msg->len - 9);

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_msg (content);
      debug_print (1, "Message received (%u): %s", content->len, text);
      g_free (text);
    }

  pattern_name = (gchar *) & content->data[VOLCA_SAMPLE_2_PATTERN_NAME_POS];
  snprintf (name, VOLCA_SAMPLE_2_PATTERN_NAME_LEN + 1, "%.*s",
	    VOLCA_SAMPLE_2_PATTERN_NAME_LEN, pattern_name);
  idata_init (pattern, content, strdup (name), NULL, NULL);
  free_msg (rx_msg);

  if (control)
    {
      task_control_set_progress (control, 1.0);
      control->part++;
    }

  return 0;
}

static gint
volca_sample_2_pattern_download (struct backend *backend, const gchar *path,
				 struct idata *pattern,
				 struct task_control *control)
{
  gint err;
  guint id;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  return volca_sample_2_pattern_download_by_id (backend, id, pattern,
						control);
}

static gint
volca_sample_2_pattern_next_dentry (struct item_iterator *iter)
{
  gint err;
  struct idata pattern;
  struct volca_sample_2_iter_data *data = iter->data;

  if (data->next > VOLCA_SAMPLE_2_PATTERN_MAX)
    {
      return -ENOENT;
    }

  err = volca_sample_2_pattern_download_by_id (data->backend, data->next,
					       &pattern, NULL);
  if (err)
    {
      return err;
    }

  item_set_name (&iter->item, "%s", pattern.name);
  iter->item.id = data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = pattern.content->len;

  (data->next)++;

  idata_free (&pattern);

  return 0;
}

static gint
volca_sample_2_pattern_read_dir (struct backend *backend,
				 struct item_iterator *iter, const gchar *dir,
				 const gchar **extensions)
{
  struct volca_sample_2_iter_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct volca_sample_2_iter_data));
  data->next = 1;
  data->backend = backend;

  item_iterator_init (iter, dir, data, volca_sample_2_pattern_next_dentry,
		      g_free);

  return 0;
}

static gint
volca_sample_2_pattern_upload (struct backend *backend, const gchar *path,
			       struct idata *pattern,
			       struct task_control *control)
{
  gint err;
  guint8 *payload;
  guint id, payload_size;
  GByteArray *tx_msg, *rx_msg;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (control)
    {
      task_control_reset (control, 1);
    }

  id--;				// O based
  if (id >= VOLCA_SAMPLE_2_PATTERN_MAX)
    {
      return -EINVAL;
    }

  payload_size = common_8bit_msg_to_midi_msg_size (pattern->content->len) + 1;
  payload = g_malloc (payload_size);
  payload[0] = id;
  common_8bit_msg_to_midi_msg (pattern->content->data, &payload[1],
			       pattern->content->len);

  tx_msg = volca_sample_2_get_msg (0x4d, payload, payload_size);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  err = volca_sample_2_get_msg_err (rx_msg);
  free_msg (rx_msg);
  if (err)
    {
      return err;
    }

  if (control)
    {
      task_control_set_progress (control, 1.0);
      control->part++;
    }

  return 0;
}

static gint
volca_sample_2_pattern_clear (struct backend *backend, const gchar *path)
{
  gint err;
  struct idata init_pattern;

  err = file_load (DATADIR "/volca_sample_2/init_pattern.vlcsplpattb",
		   &init_pattern, NULL);
  if (err)
    {
      return err;
    }

  err = volca_sample_2_pattern_upload (backend, path, &init_pattern, NULL);
  idata_free (&init_pattern);

  return err;
}

static gint
volca_sample_2_pattern_rename (struct backend *backend, const gchar *src,
			       const gchar *dst)
{
  gint err;
  gchar *name;
  struct idata pattern;

  err = volca_sample_2_pattern_download (backend, src, &pattern, NULL);
  if (err)
    {
      return err;
    }

  name = (gchar *) & pattern.content->data[VOLCA_SAMPLE_2_PATTERN_NAME_POS];
  memset (name, 0, VOLCA_SAMPLE_2_PATTERN_NAME_LEN);
  memcpy (name, dst, MIN (strlen (dst), VOLCA_SAMPLE_2_PATTERN_NAME_LEN));
  err = volca_sample_2_pattern_upload (backend, src, &pattern, NULL);

  idata_free (&pattern);

  return err;
}

static const struct fs_operations FS_VOLCA_SAMPLE_2_PATTERN_OPERATIONS = {
  .id = FS_VOLCA_SAMPLE_2_PATTERN,
  .options =
    FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SLOT_COLUMN
    | FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "pattern",
  .gui_name = "Patterns",
  .gui_icon = FS_ICON_SEQUENCE,
  .file_icon = FS_ICON_SEQUENCE,
  .max_name_len = VOLCA_SAMPLE_2_PATTERN_NAME_LEN,
  .readdir = volca_sample_2_pattern_read_dir,
  .print_item = common_print_item,
  .get_slot = volca_sample_2_get_pattern_id_as_slot,
  .delete = volca_sample_2_pattern_clear,
  .rename = volca_sample_2_pattern_rename,
  .download = volca_sample_2_pattern_download,
  .upload = volca_sample_2_pattern_upload,
  .load = file_load,
  .save = file_save,
  .get_exts = volca_sample_2_pattern_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nn
};

static guint32
volca_sample_2_get_size (guint8 *data)
{
  guint32 lsb = data[0];
  guint32 msb = data[1];
  guint sectors = lsb | (msb << 7);
  return sectors * VOLCA_SAMPLE_2_BYTES_PER_SECTOR;
}

static gint
volca_sample_2_get_storage_stats (struct backend *backend, guint8 type,
				  struct backend_storage_stats *statfs,
				  const gchar *path)
{
  gint err = 0;
  guint32 used;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = volca_sample_2_get_msg (0x1b, NULL, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (VOLCA_SAMPLE_2_GET_MSG_OP (rx_msg) != 0x4b)
    {
      err = -EIO;
      goto err;
    }

  snprintf (statfs->name, LABEL_MAX, "%s", "sample");
  statfs->bsize = volca_sample_2_get_size (&rx_msg->data[9]);
  used = volca_sample_2_get_size (&rx_msg->data[7]);
  statfs->bfree = statfs->bsize - used;

err:
  free_msg (rx_msg);
  usleep (VOLCA_SAMPLE_2_REST_TIME_US);
  return err;
}

static gint
volca_sample_2_handshake (struct backend *backend)
{
  if (memcmp (backend->midi_info.company, KORG_ID, sizeof (KORG_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  gslist_fill (&backend->fs_ops, &FS_VOLCA_SAMPLE_2_SAMPLE_OPERATIONS,
	       &FS_VOLCA_SAMPLE_2_SLICE_OPERATIONS,
	       &FS_VOLCA_SAMPLE_2_PATTERN_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "KORG Volca Sample 2");

  backend->get_storage_stats = volca_sample_2_get_storage_stats;

  return 0;
}

const struct connector CONNECTOR_VOLCA_SAMPLE_2 = {
  .name = "volca-sample-2",
  .handshake = volca_sample_2_handshake,
  .options = 0,
  .regex = ".*volca sample.*"
};
