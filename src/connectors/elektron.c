/*
 *   elektron.c
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
#include "elektron.h"
#include "elektron.h"
#include "utils.h"
#include "sample.h"
#include "package.h"
#include "common.h"
#include "../config.h"

#define DEVICES_FILE "/elektron/devices.json"

#define DEV_TAG_ID "id"
#define DEV_TAG_NAME "name"
#define DEV_TAG_ALIAS "alias"
#define DEV_TAG_FILESYSTEMS "filesystems"
#define DEV_TAG_STORAGE "storage"

static const gchar *FS_TYPE_NAMES[] = { "+Drive", "RAM" };

#define DATA_TRANSF_BLOCK_BYTES 0x2000
#define OS_TRANSF_BLOCK_BYTES 0x800
#define MAX_ZIP_SIZE (128 * 1024 * 1024)

#define FS_DATA_PRJ_PREFIX "/projects"
#define FS_DATA_SND_PREFIX "/soundbanks"
#define FS_SAMPLES_START_POS 5
#define FS_DATA_START_POS 18
#define FS_SAMPLES_SIZE_POS_W 21
#define FS_SAMPLES_LAST_FRAME_POS_W 33
#define FS_SAMPLES_PAD_RES 22

#define ELEKTRON_NAME_MAX_LEN 32

#define ELEKTRON_SAMPLE_INFO_PAD_I32_LEN 10
#define ELEKTRON_LOOP_TYPE 0x7f000000

struct elektron_sample_header
{
  guint32 type;
  guint32 sample_len_bytes;
  guint32 samplerate;
  guint32 loopstart;
  guint32 loopend;
  guint32 looptype;
  guint32 padding[ELEKTRON_SAMPLE_INFO_PAD_I32_LEN];
};

struct elektron_iterator_data
{
  GByteArray *msg;
  guint32 pos;
  guint32 hash;
  guint16 operations;
  guint8 has_valid_data;
  guint8 has_metadata;
  guint32 fs;
  gint32 max_slots;
};

enum elektron_storage
{
  STORAGE_PLUS_DRIVE = 0x1,
  STORAGE_RAM = 0x2
};

struct elektron_data
{
  guint16 seq;
  struct device_desc device_desc;
};

typedef GByteArray *(*elektron_msg_id_func) (guint);

typedef GByteArray *(*elektron_msg_id_len_func) (guint, guint);

typedef GByteArray *(*elektron_msg_path_func) (const gchar *);

typedef GByteArray *(*elektron_msg_path_len_func) (const gchar *, guint);

typedef GByteArray *(*elektron_msg_read_blk_func) (guint, guint, guint);

typedef GByteArray *(*elektron_msg_write_blk_func) (guint, GByteArray *,
						    guint *, guint, void *);

typedef void (*elektron_copy_array) (GByteArray *, GByteArray *);

typedef gint (*elektron_path_func) (struct backend *, const gchar *);

typedef gint (*elektron_src_dst_func) (struct backend *, const gchar *,
				       const gchar *);

static gint elektron_delete_samples_dir (struct backend *, const gchar *);
static gint elektron_read_samples_dir (struct backend *,
				       struct item_iterator *, const gchar *);
static gint elektron_create_samples_dir (struct backend *, const gchar *);
static gint elektron_delete_samples_item (struct backend *, const gchar *);
static gint elektron_move_samples_item (struct backend *, const gchar *,
					const gchar *);
static gint elektron_download_sample (struct backend *, const gchar *,
				      GByteArray *, struct job_control *);
static gint elektron_upload_sample (struct backend *, const gchar *,
				    GByteArray *, struct job_control *);

static gint elektron_delete_raw_dir (struct backend *, const gchar *);
static gint elektron_read_raw_dir (struct backend *, struct item_iterator *,
				   const gchar *);
static gint elektron_create_raw_dir (struct backend *, const gchar *);
static gint elektron_delete_raw_item (struct backend *, const gchar *);
static gint elektron_move_raw_item (struct backend *, const gchar *,
				    const gchar *);
static gint elektron_download_raw (struct backend *, const gchar *,
				   GByteArray *, struct job_control *);
static gint elektron_upload_raw (struct backend *, const gchar *,
				 GByteArray *, struct job_control *);

static gint elektron_read_data_dir_any (struct backend *,
					struct item_iterator *,
					const gchar *);
static gint elektron_read_data_dir_prj (struct backend *,
					struct item_iterator *,
					const gchar *);
static gint elektron_read_data_dir_snd (struct backend *,
					struct item_iterator *,
					const gchar *);

static gint elektron_move_data_item_any (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_move_data_item_prj (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_move_data_item_snd (struct backend *, const gchar *,
					 const gchar *);

static gint elektron_copy_data_item_any (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_copy_data_item_prj (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_copy_data_item_snd (struct backend *, const gchar *,
					 const gchar *);

static gint elektron_clear_data_item_any (struct backend *, const gchar *);
static gint elektron_clear_data_item_prj (struct backend *, const gchar *);
static gint elektron_clear_data_item_snd (struct backend *, const gchar *);

static gint elektron_swap_data_item_any (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_swap_data_item_prj (struct backend *, const gchar *,
					 const gchar *);
static gint elektron_swap_data_item_snd (struct backend *, const gchar *,
					 const gchar *);

static gint elektron_download_data_any (struct backend *, const gchar *,
					GByteArray *, struct job_control *);
static gint elektron_download_data_snd_pkg (struct backend *, const gchar *,
					    GByteArray *,
					    struct job_control *);
static gint elektron_download_data_prj_pkg (struct backend *, const gchar *,
					    GByteArray *,
					    struct job_control *);
static gint elektron_download_raw_pst_pkg (struct backend *, const gchar *,
					   GByteArray *,
					   struct job_control *);

static gint elektron_upload_data_any (struct backend *, const gchar *,
				      GByteArray *, struct job_control *);
static gint elektron_upload_data_prj_pkg (struct backend *, const gchar *,
					  GByteArray *, struct job_control *);
static gint elektron_upload_data_snd_pkg (struct backend *, const gchar *,
					  GByteArray *, struct job_control *);
static gint elektron_upload_raw_pst_pkg (struct backend *, const gchar *,
					 GByteArray *, struct job_control *);

static gchar *elektron_get_dev_and_fs_ext (struct backend *,
					   const struct fs_operations *);

static gchar *elektron_get_upload_path_smplrw (struct backend *,
					       const struct fs_operations *,
					       const gchar *, const gchar *);
static gchar *elektron_get_download_path (struct backend *,
					  const struct fs_operations *,
					  const gchar *, const gchar *,
					  GByteArray *);
static gchar *elektron_get_download_name (struct backend *,
					  const struct fs_operations *,
					  const gchar *);

static gint elektron_upgrade_os (struct backend *, struct sysex_transfer *);

static gint elektron_sample_load (const gchar *, GByteArray *,
				  struct job_control *);

static enum item_type elektron_get_path_type (struct backend *,
					      const gchar *,
					      fs_init_iter_func);

static void elektron_print_smplrw (struct item_iterator *, struct backend *,
				   const struct fs_operations *);

static void elektron_print_data (struct item_iterator *, struct backend *,
				 const struct fs_operations *);

static gchar *elektron_get_id_as_slot (struct item *, struct backend *);

static gboolean elektron_sample_file_exists (struct backend *, const gchar *);
static gboolean elektron_raw_file_exists (struct backend *, const gchar *);

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
static const guint8 FS_SAMPLE_GET_FILE_INFO_FROM_PATH_REQUEST[] = { 0x22 };
static const guint8 FS_SAMPLE_GET_FILE_INFO_FROM_HASH_AND_SIZE_REQUEST[] =
  { 0x23, 0, 0, 0, 0, 0, 0, 0, 0 };
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

static const guint8 FS_RAW_READ_DIR_REQUEST[] = { 0x14 };
static const guint8 FS_RAW_CREATE_DIR_REQUEST[] = { 0x15 };
static const guint8 FS_RAW_DELETE_DIR_REQUEST[] = { 0x16 };
static const guint8 FS_RAW_DELETE_FILE_REQUEST[] = { 0x24 };
static const guint8 FS_RAW_RENAME_FILE_REQUEST[] = { 0x25 };
static const guint8 FS_RAW_GET_FILE_INFO_FROM_PATH_REQUEST[] = { 0x26 };
static const guint8 FS_RAW_OPEN_FILE_READER_REQUEST[] = { 0x33 };
static const guint8 FS_RAW_CLOSE_FILE_READER_REQUEST[] = { 0x34 };
static const guint8 FS_RAW_READ_FILE_REQUEST[] =
  { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 FS_RAW_OPEN_FILE_WRITER_REQUEST[] = { 0x43, 0, 0, 0, 0 };
static const guint8 FS_RAW_CLOSE_FILE_WRITER_REQUEST[] =
  { 0x44, 0, 0, 0, 0, 0, 0, 0, 0 };
static const guint8 FS_RAW_WRITE_FILE_REQUEST[] =
  { 0x45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static const guint8 DATA_LIST_REQUEST[] = { 0x53 };
static const guint8 DATA_READ_OPEN_REQUEST[] = { 0x54 };
static const guint8 DATA_READ_PARTIAL_REQUEST[] = { 0x55 };
static const guint8 DATA_READ_CLOSE_REQUEST[] = { 0x56 };
static const guint8 DATA_WRITE_OPEN_REQUEST[] = { 0x57 };
static const guint8 DATA_WRITE_PARTIAL_REQUEST[] = { 0x58 };
static const guint8 DATA_WRITE_CLOSE_REQUEST[] = { 0x59 };
static const guint8 DATA_MOVE_REQUEST[] = { 0x5a };
static const guint8 DATA_COPY_REQUEST[] = { 0x5b };
static const guint8 DATA_CLEAR_REQUEST[] = { 0x5c };
static const guint8 DATA_SWAP_REQUEST[] = { 0x5d };
static const guint8 OS_UPGRADE_START_REQUEST[] =
  { 0x50, 0, 0, 0, 0, 's', 'y', 's', 'e', 'x', '\0', 1 };
static const guint8 OS_UPGRADE_WRITE_RESPONSE[] =
  { 0x51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static const struct fs_operations FS_SAMPLES_OPERATIONS = {
  .fs = FS_SAMPLES,
  .options = FS_OPTION_AUDIO_PLAYER | FS_OPTION_STEREO |
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = BE_FILE_ICON_WAVE,
  .type_ext = "wav",
  .max_name_len = ELEKTRON_NAME_MAX_LEN,
  .readdir = elektron_read_samples_dir,
  .file_exists = elektron_sample_file_exists,
  .print_item = elektron_print_smplrw,
  .mkdir = elektron_create_samples_dir,
  .delete = elektron_delete_samples_item,
  .rename = elektron_move_samples_item,
  .move = elektron_move_samples_item,
  .download = elektron_download_sample,
  .upload = elektron_upload_sample,
  .load = elektron_sample_load,
  .save = sample_save_from_array,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = elektron_get_upload_path_smplrw,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations FS_RAW_ANY_OPERATIONS = {
  .fs = FS_RAW_ALL,
  .options = 0,
  .name = "raw",
  .type_ext = "raw",
  .max_name_len = ELEKTRON_NAME_MAX_LEN,
  .readdir = elektron_read_raw_dir,
  .file_exists = elektron_raw_file_exists,
  .print_item = elektron_print_smplrw,
  .mkdir = elektron_create_raw_dir,
  .delete = elektron_delete_raw_item,
  .rename = elektron_move_raw_item,
  .move = elektron_move_raw_item,
  .download = elektron_download_raw,
  .upload = elektron_upload_raw,
  .load = load_file,
  .save = save_file,
  .get_ext = elektron_get_dev_and_fs_ext,
  .get_upload_path = elektron_get_upload_path_smplrw,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations FS_RAW_PRESETS_OPERATIONS = {
  .fs = FS_RAW_PRESETS,
  .options = 0,
  .name = "preset",
  .gui_name = "Presets",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "pst",
  .max_name_len = ELEKTRON_NAME_MAX_LEN,
  .readdir = elektron_read_raw_dir,
  .file_exists = elektron_raw_file_exists,
  .print_item = elektron_print_smplrw,
  .mkdir = elektron_create_raw_dir,
  .delete = elektron_delete_raw_item,
  .rename = elektron_move_raw_item,
  .move = elektron_move_raw_item,
  .download = elektron_download_raw_pst_pkg,
  .upload = elektron_upload_raw_pst_pkg,
  .load = load_file,
  .save = save_file,
  .get_ext = elektron_get_dev_and_fs_ext,
  .get_upload_path = elektron_get_upload_path_smplrw,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations FS_DATA_ANY_OPERATIONS = {
  .fs = FS_DATA_ALL,
  .options = FS_OPTION_SORT_BY_ID | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE,
  .name = "data",
  .type_ext = "data",
  .readdir = elektron_read_data_dir_any,
  .print_item = elektron_print_data,
  .delete = elektron_clear_data_item_any,
  .move = elektron_move_data_item_any,
  .copy = elektron_copy_data_item_any,
  .clear = elektron_clear_data_item_any,
  .swap = elektron_swap_data_item_any,
  .download = elektron_download_data_any,
  .upload = elektron_upload_data_any,
  .get_slot = elektron_get_id_as_slot,
  .load = load_file,
  .save = save_file,
  .get_ext = elektron_get_dev_and_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations FS_DATA_PRJ_OPERATIONS = {
  .fs = FS_DATA_PRJ,
  .options = FS_OPTION_SORT_BY_ID | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "project",
  .gui_name = "Projects",
  .gui_icon = BE_FILE_ICON_PRJ,
  .type_ext = "prj",
  .readdir = elektron_read_data_dir_prj,
  .print_item = elektron_print_data,
  .delete = elektron_clear_data_item_prj,
  .move = elektron_move_data_item_prj,
  .copy = elektron_copy_data_item_prj,
  .clear = elektron_clear_data_item_prj,
  .swap = elektron_swap_data_item_prj,
  .download = elektron_download_data_prj_pkg,
  .upload = elektron_upload_data_prj_pkg,
  .get_slot = elektron_get_id_as_slot,
  .load = load_file,
  .save = save_file,
  .get_ext = elektron_get_dev_and_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations FS_DATA_SND_OPERATIONS = {
  .fs = FS_DATA_SND,
  .options = FS_OPTION_SORT_BY_ID | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SHOW_SIZE_COLUMN | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN,
  .name = "sound",
  .gui_name = "Sounds",
  .gui_icon = BE_FILE_ICON_SND,
  .type_ext = "snd",
  .readdir = elektron_read_data_dir_snd,
  .print_item = elektron_print_data,
  .delete = elektron_clear_data_item_snd,
  .move = elektron_move_data_item_snd,
  .copy = elektron_copy_data_item_snd,
  .clear = elektron_clear_data_item_snd,
  .swap = elektron_swap_data_item_snd,
  .download = elektron_download_data_snd_pkg,
  .upload = elektron_upload_data_snd_pkg,
  .get_slot = elektron_get_id_as_slot,
  .load = load_file,
  .save = save_file,
  .get_ext = elektron_get_dev_and_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = elektron_get_download_path
};

static const struct fs_operations *FS_OPERATIONS[] = {
  &FS_SAMPLES_OPERATIONS, &FS_RAW_ANY_OPERATIONS, &FS_RAW_PRESETS_OPERATIONS,
  &FS_DATA_ANY_OPERATIONS, &FS_DATA_PRJ_OPERATIONS, &FS_DATA_SND_OPERATIONS,
  NULL
};

static gchar *
elektron_get_id_as_slot (struct item *item, struct backend *backend)
{
  gchar *slot = malloc (LABEL_MAX);
  if (item->id >= 0)
    {
      snprintf (slot, LABEL_MAX, "%03d", item->id);
    }
  else
    {
      slot[0] = 0;
    }
  return slot;
}

static void
elektron_print_smplrw (struct item_iterator *iter,
		       struct backend *backend,
		       const struct fs_operations *fs_ops)
{
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  struct elektron_iterator_data *data = iter->data;

  printf ("%c %10s %08x %s\n", iter->item.type,
	  hsize, data->hash, iter->item.name);
  g_free (hsize);
}

static void
elektron_print_data (struct item_iterator *iter,
		     struct backend *backend,
		     const struct fs_operations *fs_ops)
{
  gchar *hsize = get_human_size (iter->item.size, FALSE);
  struct elektron_iterator_data *data = iter->data;
  gchar *slot = iter->item.id > 0 ?
    elektron_get_id_as_slot (&iter->item, backend) : strdup (" -1");
  printf ("%c %04x %d %d %10s %s %s\n", iter->item.type, data->operations,
	  data->has_valid_data, data->has_metadata, hsize, slot,
	  iter->item.name);
  g_free (hsize);
  g_free (slot);
}

static void
elektron_free_iterator_data (void *iter_data)
{
  struct elektron_iterator_data *data = iter_data;
  free_msg (data->msg);
  g_free (data);
}

static inline void
elektron_get_utf8 (gchar * dst, const gchar * s)
{
  gchar *aux = g_convert (s, -1, "UTF8", "CP1252", NULL, NULL, NULL);
  snprintf (dst, LABEL_MAX, "%s", aux);
  g_free (aux);
}

static inline gchar *
elektron_get_cp1252 (const gchar * s)
{
  return g_convert (s, -1, "CP1252", "UTF8", NULL, NULL, NULL);
}

static inline guint8
elektron_get_msg_status (const GByteArray * msg)
{
  return msg->data[5];
}

static inline gchar *
elektron_get_msg_string (const GByteArray * msg)
{
  return (gchar *) & msg->data[6];
}

static guint
elektron_next_smplrw_entry (struct item_iterator *iter)
{
  guint32 *data32;
  gchar *name_cp1252;
  struct elektron_iterator_data *data = iter->data;

  if (data->pos == data->msg->len)
    {
      return -ENOENT;
    }
  else
    {
      data32 = (guint32 *) & data->msg->data[data->pos];
      data->hash = be32toh (*data32);
      data->pos += sizeof (guint32);

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->item.size = be32toh (*data32);
      data->pos += sizeof (guint32);

      data->pos++;		//write_protected

      iter->item.type = data->msg->data[data->pos];
      data->pos++;

      name_cp1252 = (gchar *) & data->msg->data[data->pos];
      elektron_get_utf8 (iter->item.name, name_cp1252);
      if (data->fs == FS_RAW_ALL && iter->item.type == ELEKTROID_FILE)
	{
	  //This eliminates the extension ".mc-snd" that the device provides.
	  iter->item.name[strlen (iter->item.name) - 7] = 0;
	}
      data->pos += strlen (name_cp1252) + 1;

      iter->item.id = -1;

      return 0;
    }
}

static gint
elektron_init_iterator (struct item_iterator *iter, GByteArray * msg,
			iterator_next next, enum elektron_fs fs,
			gint32 max_slots)
{
  struct elektron_iterator_data *data =
    malloc (sizeof (struct elektron_iterator_data));

  data->msg = msg;
  data->pos = fs == FS_DATA_ALL ? FS_DATA_START_POS : FS_SAMPLES_START_POS;
  data->fs = fs;
  data->max_slots = max_slots;

  iter->data = data;
  iter->next = next;
  iter->free = elektron_free_iterator_data;
  iter->item.id = 0;
  iter->item.type = ELEKTROID_NONE;

  return 0;
}

static GByteArray *
elektron_decode_payload (const GByteArray * src)
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
elektron_encode_payload (const GByteArray * src)
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
elektron_msg_to_raw (const GByteArray * msg)
{
  GByteArray *encoded;
  GByteArray *sysex = g_byte_array_new ();

  g_byte_array_append (sysex, MSG_HEADER, sizeof (MSG_HEADER));
  encoded = elektron_encode_payload (msg);
  g_byte_array_append (sysex, encoded->data, encoded->len);
  free_msg (encoded);
  g_byte_array_append (sysex, (guint8 *) "\xf7", 1);

  return sysex;
}

static gint
elektron_get_smplrw_info_from_msg (GByteArray * info_msg, guint32 * id,
				   guint * size)
{
  if (elektron_get_msg_status (info_msg))
    {
      if (id)
	{
	  *id = be32toh (*((guint32 *) & info_msg->data[6]));
	}
      if (size)
	{
	  *size = be32toh (*((guint32 *) & info_msg->data[10]));
	}
    }
  else
    {
      if (id)
	{
	  return -EIO;
	}
    }

  return 0;
}

static GByteArray *
elektron_new_msg (const guint8 * data, guint len)
{
  GByteArray *msg = g_byte_array_new ();

  g_byte_array_append (msg, (guchar *) "\0\0\0\0", 4);
  g_byte_array_append (msg, data, len);

  return msg;
}

static GByteArray *
elektron_new_msg_uint8 (const guint8 * data, guint len, guint8 type)
{
  GByteArray *msg = elektron_new_msg (data, len);

  g_byte_array_append (msg, &type, 1);

  return msg;
}

static GByteArray *
elektron_new_msg_path (const guint8 * data, guint len, const gchar * path)
{
  GByteArray *msg;
  gchar *path_cp1252 = elektron_get_cp1252 (path);

  if (!path_cp1252)
    {
      return NULL;
    }

  msg = elektron_new_msg (data, len);
  g_byte_array_append (msg, (guchar *) path_cp1252, strlen (path_cp1252) + 1);
  g_free (path_cp1252);

  return msg;
}

static GByteArray *
elektron_new_msg_close_common_read (const guint8 * data, guint len, guint id)
{
  guint32 aux32;
  GByteArray *msg = elektron_new_msg (data, len);

  aux32 = htobe32 (id);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (guint32));
  return msg;
}

static GByteArray *
elektron_new_msg_close_sample_read (guint id)
{
  return
    elektron_new_msg_close_common_read (FS_SAMPLE_CLOSE_FILE_READER_REQUEST,
					sizeof
					(FS_SAMPLE_CLOSE_FILE_READER_REQUEST),
					id);
}

static GByteArray *
elektron_new_msg_close_raw_read (guint id)
{
  return
    elektron_new_msg_close_common_read (FS_RAW_CLOSE_FILE_READER_REQUEST,
					sizeof
					(FS_RAW_CLOSE_FILE_READER_REQUEST),
					id);
}

static GByteArray *
elektron_new_msg_open_common_write (const guint8 * data, guint len,
				    const gchar * path, guint bytes)
{
  guint32 aux32;
  GByteArray *msg = elektron_new_msg_path (data, len, path);

  aux32 = htobe32 (bytes);
  memcpy (&msg->data[5], &aux32, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_open_sample_write (const gchar * path, guint bytes)
{
  return
    elektron_new_msg_open_common_write (FS_SAMPLE_OPEN_FILE_WRITER_REQUEST,
					sizeof
					(FS_SAMPLE_OPEN_FILE_WRITER_REQUEST),
					path,
					bytes +
					sizeof (struct
						elektron_sample_header));
}

static GByteArray *
elektron_new_msg_open_raw_write (const gchar * path, guint bytes)
{
  return elektron_new_msg_open_common_write (FS_RAW_OPEN_FILE_WRITER_REQUEST,
					     sizeof
					     (FS_RAW_OPEN_FILE_WRITER_REQUEST),
					     path, bytes);
}


static GByteArray *
elektron_new_msg_list (const gchar * path, int32_t start_index,
		       int32_t end_index, gboolean all)
{
  guint32 aux32;
  guint8 aux8;
  GByteArray *msg = elektron_new_msg_path (DATA_LIST_REQUEST,
					   sizeof (DATA_LIST_REQUEST),
					   path);

  aux32 = htobe32 (start_index);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (guint32));
  aux32 = htobe32 (end_index);
  g_byte_array_append (msg, (guchar *) & aux32, sizeof (guint32));
  aux8 = all;
  g_byte_array_append (msg, (guchar *) & aux8, sizeof (guint8));

  return msg;
}

static GByteArray *
elektron_new_msg_write_sample_blk (guint id, GByteArray * sample,
				   guint * total, guint seq, void *data)
{
  guint32 aux32;
  guint16 aux16, *aux16p;
  int i, consumed, bytes_blk;
  struct sample_info *sample_info = data;
  struct elektron_sample_header elektron_sample_header;
  GByteArray *msg = elektron_new_msg (FS_SAMPLE_WRITE_FILE_REQUEST,
				      sizeof (FS_SAMPLE_WRITE_FILE_REQUEST));


  aux32 = htobe32 (id);
  memcpy (&msg->data[5], &aux32, sizeof (guint32));
  aux32 = htobe32 (DATA_TRANSF_BLOCK_BYTES * seq);
  memcpy (&msg->data[13], &aux32, sizeof (guint32));

  bytes_blk = DATA_TRANSF_BLOCK_BYTES;
  consumed = 0;

  if (seq == 0)
    {
      elektron_sample_header.type = 0;
      elektron_sample_header.sample_len_bytes = htobe32 (sample->len);
      elektron_sample_header.samplerate = htobe32 (ELEKTRON_SAMPLE_RATE);
      elektron_sample_header.loopstart = htobe32 (sample_info->loopstart);
      elektron_sample_header.loopend = htobe32 (sample_info->loopend);
      elektron_sample_header.looptype = htobe32 (ELEKTRON_LOOP_TYPE);
      memset (&elektron_sample_header.padding, 0,
	      sizeof (guint32) * ELEKTRON_SAMPLE_INFO_PAD_I32_LEN);

      g_byte_array_append (msg, (guchar *) & elektron_sample_header,
			   sizeof (struct elektron_sample_header));

      consumed = sizeof (struct elektron_sample_header);
      bytes_blk -= consumed;
    }

  i = 0;
  aux16p = (guint16 *) & sample->data[*total];
  while (i < bytes_blk && *total < sample->len)
    {
      aux16 = htobe16 (*aux16p);
      g_byte_array_append (msg, (guint8 *) & aux16, sizeof (guint16));
      aux16p++;
      (*total) += sizeof (guint16);
      consumed += sizeof (guint16);
      i += sizeof (guint16);
    }

  aux32 = htobe32 (consumed);
  memcpy (&msg->data[9], &aux32, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_write_raw_blk (guint id, GByteArray * raw, guint * total,
				guint seq, void *data)
{
  gint len;
  guint32 aux32;
  GByteArray *msg = elektron_new_msg (FS_RAW_WRITE_FILE_REQUEST,
				      sizeof (FS_RAW_WRITE_FILE_REQUEST));

  aux32 = htobe32 (id);
  memcpy (&msg->data[5], &aux32, sizeof (guint32));
  aux32 = htobe32 (DATA_TRANSF_BLOCK_BYTES * seq);
  memcpy (&msg->data[13], &aux32, sizeof (guint32));

  len = raw->len - *total;
  len = len > DATA_TRANSF_BLOCK_BYTES ? DATA_TRANSF_BLOCK_BYTES : len;
  g_byte_array_append (msg, &raw->data[*total], len);
  (*total) += len;

  aux32 = htobe32 (len);
  memcpy (&msg->data[9], &aux32, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_close_common_write (const guint8 * data, guint len,
				     guint id, guint bytes)
{
  guint32 aux32;
  GByteArray *msg = elektron_new_msg (data, len);

  aux32 = htobe32 (id);
  memcpy (&msg->data[5], &aux32, sizeof (guint32));
  aux32 = htobe32 (bytes);
  memcpy (&msg->data[9], &aux32, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_close_sample_write (guint id, guint bytes)
{
  return
    elektron_new_msg_close_common_write (FS_SAMPLE_CLOSE_FILE_WRITER_REQUEST,
					 sizeof
					 (FS_SAMPLE_CLOSE_FILE_WRITER_REQUEST),
					 id,
					 bytes +
					 sizeof (struct
						 elektron_sample_header));
}

static GByteArray *
elektron_new_msg_close_raw_write (guint id, guint bytes)
{
  return
    elektron_new_msg_close_common_write (FS_RAW_CLOSE_FILE_WRITER_REQUEST,
					 sizeof
					 (FS_RAW_CLOSE_FILE_WRITER_REQUEST),
					 id, bytes);
}

static GByteArray *
elektron_new_msg_read_common_blk (const guint8 * data, guint len, guint id,
				  guint start, guint size)
{
  guint32 aux;
  GByteArray *msg = elektron_new_msg (data, len);

  aux = htobe32 (id);
  memcpy (&msg->data[5], &aux, sizeof (guint32));
  aux = htobe32 (size);
  memcpy (&msg->data[9], &aux, sizeof (guint32));
  aux = htobe32 (start);
  memcpy (&msg->data[13], &aux, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_read_sample_blk (guint id, guint start, guint size)
{
  return elektron_new_msg_read_common_blk (FS_SAMPLE_READ_FILE_REQUEST,
					   sizeof
					   (FS_SAMPLE_READ_FILE_REQUEST), id,
					   start, size);
}

static GByteArray *
elektron_new_msg_read_raw_blk (guint id, guint start, guint size)
{
  return elektron_new_msg_read_common_blk (FS_RAW_READ_FILE_REQUEST,
					   sizeof (FS_RAW_READ_FILE_REQUEST),
					   id, start, size);
}

static GByteArray *
elektron_raw_to_msg (GByteArray * sysex)
{
  GByteArray *msg;
  GByteArray *payload;
  gint len = sysex->len - sizeof (MSG_HEADER) - 1;

  if (len > 0)
    {
      payload = g_byte_array_new ();
      g_byte_array_append (payload, &sysex->data[sizeof (MSG_HEADER)], len);
      msg = elektron_decode_payload (payload);
      free_msg (payload);
    }
  else
    {
      msg = NULL;
    }

  return msg;
}

static gint
elektron_tx (struct backend *backend, const GByteArray * msg)
{
  gint res;
  guint16 aux;
  gchar *text;
  struct sysex_transfer transfer;
  struct elektron_data *data = backend->data;

  aux = htobe16 (data->seq);
  memcpy (msg->data, &aux, sizeof (guint16));
  data->seq++;

  transfer.raw = elektron_msg_to_raw (msg);
  res = backend_tx_sysex (backend, &transfer);
  if (!res)
    {
      text = debug_get_hex_msg (msg);
      debug_print (1, "Message sent (%d): %s\n", msg->len, text);
      free (text);
    }

  free_msg (transfer.raw);
  return res;
}

static GByteArray *
elektron_rx (struct backend *backend, gint timeout)
{
  gchar *text;
  GByteArray *msg;
  struct sysex_transfer transfer;

  transfer.timeout = timeout;
  transfer.batch = FALSE;

  while (1)
    {
      if (backend_rx_sysex (backend, &transfer))
	{
	  return NULL;
	}

      if (transfer.raw->len >= 12
	  && !memcmp (transfer.raw->data, MSG_HEADER, 6))
	{
	  break;
	}

      if (debug_level > 1)
	{
	  text = debug_get_hex_msg (transfer.raw);
	  debug_print (2, "Message skipped (%d): %s\n", transfer.raw->len,
		       text);
	  free (text);
	}
      free_msg (transfer.raw);
    }

  msg = elektron_raw_to_msg (transfer.raw);
  if (msg)
    {
      text = debug_get_hex_msg (msg);
      debug_print (1, "Message received (%d): %s\n", msg->len, text);
      free (text);
    }

  free_msg (transfer.raw);
  return msg;
}

//Not synchronized. Only meant to be called from elektron_tx_and_rx_timeout.

static GByteArray *
elektron_tx_and_rx_timeout_no_cache (struct backend *backend,
				     GByteArray * tx_msg, gint timeout)
{
  ssize_t len;
  GByteArray *rx_msg;
  guint msg_type = tx_msg->data[4] | 0x80;

  len = elektron_tx (backend, tx_msg);
  if (len < 0)
    {
      rx_msg = NULL;
      goto cleanup;
    }

  rx_msg = elektron_rx (backend, timeout < 0 ? BE_SYSEX_TIMEOUT_MS : timeout);
  if (rx_msg && rx_msg->data[4] != msg_type)
    {
      error_print ("Illegal message type in response\n");
      free_msg (rx_msg);
      rx_msg = NULL;
      goto cleanup;
    }

cleanup:
  free_msg (tx_msg);
  return rx_msg;
}

//Synchronized

static GByteArray *
elektron_tx_and_rx_timeout (struct backend *backend, GByteArray * tx_msg,
			    gint timeout)
{
  GBytes *key;
  GByteArray *rx_msg, *ret = NULL;

  g_mutex_lock (&backend->mutex);
  if (backend->cache)
    {
      key = g_bytes_new (tx_msg->data, tx_msg->len);
      rx_msg = g_hash_table_lookup (backend->cache, key);
      if (rx_msg)
	{
	  ret = g_byte_array_sized_new (rx_msg->len);
	  g_byte_array_append (ret, rx_msg->data, rx_msg->len);
	  g_bytes_unref (key);
	  goto end;
	}

      rx_msg = elektron_tx_and_rx_timeout_no_cache (backend, tx_msg, timeout);
      if (!rx_msg)
	{
	  g_bytes_unref (key);
	  goto end;
	}

      ret = g_byte_array_sized_new (rx_msg->len);
      g_byte_array_append (ret, rx_msg->data, rx_msg->len);
      g_hash_table_insert (backend->cache, key, rx_msg);
    }
  else
    {
      ret = elektron_tx_and_rx_timeout_no_cache (backend, tx_msg, timeout);
    }

end:
  g_mutex_unlock (&backend->mutex);
  return ret;
}

static GByteArray *
elektron_tx_and_rx (struct backend *backend, GByteArray * tx_msg)
{
  return elektron_tx_and_rx_timeout (backend, tx_msg, -1);
}

static enum item_type
elektron_get_path_type (struct backend *backend, const gchar * path,
			fs_init_iter_func init_iter)
{
  gchar *dir, *name;
  enum item_type res;
  struct item_iterator iter;

  if (strcmp (path, "/") == 0)
    {
      return ELEKTROID_DIR;
    }

  name = g_path_get_basename (path);
  dir = g_path_get_dirname (path);
  res = ELEKTROID_NONE;
  if (!init_iter (backend, &iter, dir))
    {
      while (!next_item_iterator (&iter))
	{
	  if (strcmp (name, iter.item.name) == 0)
	    {
	      res = iter.item.type;
	      break;
	    }
	}
      free_item_iterator (&iter);
    }

  g_free (name);
  g_free (dir);
  return res;
}

static gint
elektron_read_common_dir (struct backend *backend,
			  struct item_iterator *iter, const gchar * dir,
			  const guint8 msg[], int size,
			  fs_init_iter_func init_iter, enum elektron_fs fs,
			  fs_file_exists file_exists)
{
  GByteArray *tx_msg, *rx_msg = NULL;

  if (file_exists (backend, dir))
    {
      return -ENOTDIR;
    }

  tx_msg = elektron_new_msg_path (msg, size, dir);
  if (!tx_msg)
    {
      return -EINVAL;
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }

  if (rx_msg->len == 5 &&
      elektron_get_path_type (backend, dir, init_iter) != ELEKTROID_DIR)
    {
      free_msg (rx_msg);
      return -ENOTDIR;
    }

  return elektron_init_iterator (iter, rx_msg, elektron_next_smplrw_entry,
				 fs, -1);
}

static gint
elektron_read_samples_dir (struct backend *backend,
			   struct item_iterator *iter, const gchar * dir)
{
  return elektron_read_common_dir (backend, iter, dir,
				   FS_SAMPLE_READ_DIR_REQUEST,
				   sizeof (FS_SAMPLE_READ_DIR_REQUEST),
				   elektron_read_samples_dir, FS_SAMPLES,
				   elektron_sample_file_exists);
}

static gint
elektron_read_raw_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar * dir)
{
  return elektron_read_common_dir (backend, iter, dir,
				   FS_RAW_READ_DIR_REQUEST,
				   sizeof (FS_RAW_READ_DIR_REQUEST),
				   elektron_read_raw_dir, FS_RAW_ALL,
				   elektron_raw_file_exists);
}

static gint
elektron_src_dst_common (struct backend *backend,
			 const gchar * src, const gchar * dst,
			 const guint8 * data, guint len)
{
  gint res;
  GByteArray *rx_msg;
  GByteArray *tx_msg = elektron_new_msg (data, len);

  gchar *dst_cp1252 = elektron_get_cp1252 (dst);
  if (!dst_cp1252)
    {
      return -EINVAL;
    }

  gchar *src_cp1252 = elektron_get_cp1252 (src);
  if (!src_cp1252)
    {
      g_free (dst_cp1252);
      return -EINVAL;
    }

  g_byte_array_append (tx_msg, (guchar *) src_cp1252,
		       strlen (src_cp1252) + 1);
  g_byte_array_append (tx_msg, (guchar *) dst_cp1252,
		       strlen (dst_cp1252) + 1);

  g_free (src_cp1252);
  g_free (dst_cp1252);

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }
  //Response: x, x, x, x, 0xa1, [0 (error), 1 (success)]...
  if (elektron_get_msg_status (rx_msg))
    {
      res = 0;
    }
  else
    {
      res = -EPERM;
      error_print ("%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
    }
  free_msg (rx_msg);

  return res;
}

static gint
elektron_rename_sample_file (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_src_dst_common (backend, src, dst,
				  FS_SAMPLE_RENAME_FILE_REQUEST,
				  sizeof (FS_SAMPLE_RENAME_FILE_REQUEST));
}

static gint
elektron_rename_raw_file (struct backend *backend, const gchar * src,
			  const gchar * dst)
{
  return elektron_src_dst_common (backend, src, dst,
				  FS_RAW_RENAME_FILE_REQUEST,
				  sizeof (FS_RAW_RENAME_FILE_REQUEST));
}

static gint
elektron_move_common_item (struct backend *backend, const gchar * src,
			   const gchar * dst, fs_init_iter_func init_iter,
			   elektron_src_dst_func mv, fs_path_func mkdir,
			   elektron_path_func rmdir)
{
  enum item_type type;
  gint res;
  gchar *src_plus;
  gchar *dst_plus;
  struct item_iterator iter;

  //Renaming is not implemented for directories so we need to implement it.

  debug_print (1, "Renaming remotely from %s to %s...\n", src, dst);

  type = elektron_get_path_type (backend, src, init_iter);
  if (type == ELEKTROID_FILE)
    {
      return mv (backend, src, dst);
    }
  else if (type == ELEKTROID_DIR)
    {
      res = mkdir (backend, dst);
      if (res)
	{
	  return res;
	}
      if (!init_iter (backend, &iter, src))
	{
	  while (!next_item_iterator (&iter) && !res)
	    {
	      src_plus = chain_path (src, iter.item.name);
	      dst_plus = chain_path (dst, iter.item.name);
	      res = elektron_move_common_item (backend, src_plus, dst_plus,
					       init_iter, mv, mkdir, rmdir);
	      free (src_plus);
	      free (dst_plus);
	    }
	  free_item_iterator (&iter);
	}
      if (!res)
	{
	  res = rmdir (backend, src);
	}
      return res;
    }
  else
    {
      return -EBADF;
    }
}

static gint
elektron_path_common (struct backend *backend, const gchar * path,
		      const guint8 * template, gint size)
{
  gint res;
  GByteArray *rx_msg;
  GByteArray *tx_msg;

  tx_msg = elektron_new_msg_path (template, size, path);
  if (!tx_msg)
    {
      return -EINVAL;
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }
  //Response: x, x, x, x, 0xX0, [0 (error), 1 (success)]...
  if (elektron_get_msg_status (rx_msg))
    {
      res = 0;
    }
  else
    {
      res = -EPERM;
      debug_print (1, "%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
    }
  free_msg (rx_msg);

  return res;
}

static gint
elektron_delete_sample (struct backend *backend, const gchar * path)
{
  return elektron_path_common (backend, path,
			       FS_SAMPLE_DELETE_FILE_REQUEST,
			       sizeof (FS_SAMPLE_DELETE_FILE_REQUEST));
}

static gint
elektron_delete_samples_dir (struct backend *backend, const gchar * path)
{
  return elektron_path_common (backend, path, FS_SAMPLE_DELETE_DIR_REQUEST,
			       sizeof (FS_SAMPLE_DELETE_DIR_REQUEST));
}

//This adds back the extension ".mc-snd" that the device provides.
static gchar *
elektron_add_ext_to_mc_snd (const gchar * path)
{
  gchar *path_with_ext;
  GString *str = g_string_new (path);
  g_string_append (str, ".mc-snd");
  path_with_ext = str->str;
  g_string_free (str, FALSE);
  return path_with_ext;
}

static gboolean
elektron_sample_file_exists (struct backend *backend, const gchar * path)
{
  gint res = elektron_path_common (backend, path,
				   FS_SAMPLE_GET_FILE_INFO_FROM_PATH_REQUEST,
				   sizeof
				   (FS_SAMPLE_GET_FILE_INFO_FROM_PATH_REQUEST));
  return res == 0;
}

static gboolean
elektron_raw_file_exists (struct backend *backend, const gchar * path)
{
  gchar *name_with_ext = elektron_add_ext_to_mc_snd (path);
  gint res = elektron_path_common (backend, path,
				   FS_RAW_GET_FILE_INFO_FROM_PATH_REQUEST,
				   sizeof
				   (FS_RAW_GET_FILE_INFO_FROM_PATH_REQUEST));
  g_free (name_with_ext);
  return res == 0;
}

static gint
elektron_delete_raw (struct backend *backend, const gchar * path)
{
  gint ret;
  gchar *path_with_ext = elektron_add_ext_to_mc_snd (path);
  ret = elektron_path_common (backend, path_with_ext,
			      FS_RAW_DELETE_FILE_REQUEST,
			      sizeof (FS_RAW_DELETE_FILE_REQUEST));
  g_free (path_with_ext);
  return ret;
}

static gint
elektron_delete_raw_dir (struct backend *backend, const gchar * path)
{
  return elektron_path_common (backend, path, FS_RAW_DELETE_DIR_REQUEST,
			       sizeof (FS_RAW_DELETE_DIR_REQUEST));
}

static gint
elektron_create_samples_dir (struct backend *backend, const gchar * path)
{
  return elektron_path_common (backend, path, FS_SAMPLE_CREATE_DIR_REQUEST,
			       sizeof (FS_SAMPLE_CREATE_DIR_REQUEST));
}

static gint
elektron_move_samples_item (struct backend *backend, const gchar * src,
			    const gchar * dst)
{
  return elektron_move_common_item (backend, src, dst,
				    elektron_read_samples_dir,
				    elektron_rename_sample_file,
				    elektron_create_samples_dir,
				    elektron_delete_samples_dir);
}

static gint
elektron_create_raw_dir (struct backend *backend, const gchar * path)
{
  return elektron_path_common (backend, path, FS_RAW_CREATE_DIR_REQUEST,
			       sizeof (FS_RAW_CREATE_DIR_REQUEST));
}

static gint
elektron_move_raw_item (struct backend *backend, const gchar * src,
			const gchar * dst)
{
  gint ret;
  gchar *src_with_ext = elektron_add_ext_to_mc_snd (src);
  ret = elektron_move_common_item (backend, src_with_ext, dst,
				   elektron_read_raw_dir,
				   elektron_rename_raw_file,
				   elektron_create_raw_dir,
				   elektron_delete_raw_dir);
  g_free (src_with_ext);
  return ret;
}

static gint
elektron_delete_common_item (struct backend *backend, const gchar * path,
			     fs_init_iter_func init_iter,
			     elektron_path_func rmdir, elektron_path_func rm)
{
  enum item_type type;
  gchar *new_path;
  struct item_iterator iter;
  gint res;

  type = elektron_get_path_type (backend, path, init_iter);
  if (type == ELEKTROID_FILE)
    {
      return rm (backend, path);
    }
  else if (type == ELEKTROID_DIR)
    {
      debug_print (1, "Deleting %s samples dir...\n", path);

      if (init_iter (backend, &iter, path))
	{
	  error_print ("Error while opening samples dir %s dir\n", path);
	  res = -EINVAL;
	}
      else
	{
	  res = 0;
	  while (!res && !next_item_iterator (&iter))
	    {
	      new_path = chain_path (path, iter.item.name);
	      res = res || elektron_delete_common_item (backend, new_path,
							init_iter, rmdir, rm);
	      free (new_path);
	    }
	  free_item_iterator (&iter);
	}
      return res || rmdir (backend, path);
    }
  else
    {
      return -EBADF;
    }
}

static gint
elektron_delete_samples_item (struct backend *backend, const gchar * path)
{
  return elektron_delete_common_item (backend, path,
				      elektron_read_samples_dir,
				      elektron_delete_samples_dir,
				      elektron_delete_sample);
}

static gint
elektron_delete_raw_item (struct backend *backend, const gchar * path)
{
  return elektron_delete_common_item (backend, path,
				      elektron_read_raw_dir,
				      elektron_delete_raw_dir,
				      elektron_delete_raw);
}

static gint
elektron_upload_smplrw (struct backend *backend, const gchar * path,
			GByteArray * input, struct job_control *control,
			elektron_msg_path_len_func new_msg_open_write,
			elektron_msg_write_blk_func new_msg_write_blk,
			elektron_msg_id_len_func new_msg_close_write)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  guint transferred;
  guint32 id;
  int i;
  gboolean active;
  gint res = 0;

  //If the file already exists the device makes no difference between creating a new file and creating an already existent file.
  //Also, the new file would be discarded if an upload is not completed.

  tx_msg = new_msg_open_write (path, input->len);
  if (!tx_msg)
    {
      return -EINVAL;
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }

  //Response: x, x, x, x, 0xc0, [0 (error), 1 (success)], id, frames
  res = elektron_get_smplrw_info_from_msg (rx_msg, &id, NULL);
  if (res)
    {
      error_print ("%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return res;
    }
  free_msg (rx_msg);

  transferred = 0;
  i = 0;

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  while (transferred < input->len && active)
    {
      tx_msg = new_msg_write_blk (id, input, &transferred, i, control->data);
      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (!rx_msg)
	{
	  return -EIO;
	}
      //Response: x, x, x, x, 0xc2, [0 (error), 1 (success)]...
      if (!elektron_get_msg_status (rx_msg))
	{
	  error_print ("Unexpected status\n");
	}
      free_msg (rx_msg);
      i++;

      set_job_control_progress (control, transferred / (double) input->len);
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      usleep (BE_REST_TIME_US);
    }

  debug_print (2, "%d bytes sent\n", transferred);

  if (active)
    {
      tx_msg = new_msg_close_write (id, transferred);
      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (!rx_msg)
	{
	  return -EIO;
	}
      //Response: x, x, x, x, 0xc1, [0 (error), 1 (success)]...
      if (!elektron_get_msg_status (rx_msg))
	{
	  error_print ("Unexpected status\n");
	}
      free_msg (rx_msg);
    }

  return res;
}

gint
elektron_upload_sample_part (struct backend *backend, const gchar * path,
			     GByteArray * sample, struct job_control *control)
{
  return elektron_upload_smplrw (backend, path, sample, control,
				 elektron_new_msg_open_sample_write,
				 elektron_new_msg_write_sample_blk,
				 elektron_new_msg_close_sample_write);
}

static gint
elektron_upload_sample (struct backend *backend, const gchar * path,
			GByteArray * input, struct job_control *control)
{
  control->parts = 1;
  control->part = 0;
  return elektron_upload_sample_part (backend, path, input, control);
}

static gint
elektron_upload_raw (struct backend *backend, const gchar * path,
		     GByteArray * sample, struct job_control *control)
{
  return elektron_upload_smplrw (backend, path, sample, control,
				 elektron_new_msg_open_raw_write,
				 elektron_new_msg_write_raw_blk,
				 elektron_new_msg_close_raw_write);
}

static GByteArray *
elektron_new_msg_open_sample_read (const gchar * path)
{
  return elektron_new_msg_path (FS_SAMPLE_OPEN_FILE_READER_REQUEST,
				sizeof
				(FS_SAMPLE_OPEN_FILE_READER_REQUEST), path);
}

static GByteArray *
elektron_new_msg_open_raw_read (const gchar * path)
{
  return elektron_new_msg_path (FS_RAW_OPEN_FILE_READER_REQUEST,
				sizeof
				(FS_RAW_OPEN_FILE_READER_REQUEST), path);
}

static void
elektron_copy_sample_data (GByteArray * input, GByteArray * output)
{
  gint i;
  gint16 v;
  gint16 *frame = (gint16 *) input->data;

  for (i = 0; i < input->len; i += sizeof (gint16))
    {
      v = be16toh (*frame);
      g_byte_array_append (output, (guint8 *) & v, sizeof (gint16));
      frame++;
    }
}

static void
elektron_copy_raw_data (GByteArray * input, GByteArray * output)
{
  g_byte_array_append (output, input->data, input->len);
}

static gint
elektron_download_smplrw (struct backend *backend, const gchar * path,
			  GByteArray * output, struct job_control *control,
			  elektron_msg_path_func new_msg_open_read,
			  guint read_offset,
			  elektron_msg_read_blk_func new_msg_read_blk,
			  elektron_msg_id_func new_msg_close_read,
			  elektron_copy_array copy_array)
{
  struct sample_info *sample_info;
  struct elektron_sample_header *elektron_sample_header;
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  GByteArray *array;
  guint32 id;
  guint frames;
  guint next_block_start;
  guint req_size;
  guint offset;
  gboolean active;
  gint res;

  tx_msg = new_msg_open_read (path);
  if (!tx_msg)
    {
      return -EINVAL;
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }
  res = elektron_get_smplrw_info_from_msg (rx_msg, &id, &frames);
  if (res)
    {
      error_print ("%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return res;
    }
  free_msg (rx_msg);

  debug_print (2, "%d frames to download\n", frames);

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  array = g_byte_array_new ();
  res = 0;
  next_block_start = 0;
  offset = read_offset;
  control->data = NULL;
  while (next_block_start < frames && active)
    {
      req_size =
	frames - next_block_start >
	DATA_TRANSF_BLOCK_BYTES ? DATA_TRANSF_BLOCK_BYTES : frames -
	next_block_start;
      tx_msg = new_msg_read_blk (id, next_block_start, req_size);
      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (!rx_msg)
	{
	  res = -EIO;
	  goto cleanup;
	}
      g_byte_array_append (array, &rx_msg->data[FS_SAMPLES_PAD_RES + offset],
			   req_size - offset);

      next_block_start += req_size;
      //Only in the first iteration. It has no effect for the raw filesystem (M:C) as offset is 0.
      if (offset)
	{
	  offset = 0;
	  elektron_sample_header =
	    (struct elektron_sample_header *)
	    &rx_msg->data[FS_SAMPLES_PAD_RES];
	  sample_info = malloc (sizeof (struct elektron_sample_header));
	  sample_info->loopstart =
	    be32toh (elektron_sample_header->loopstart);
	  sample_info->loopend = be32toh (elektron_sample_header->loopend);
	  sample_info->looptype = elektron_sample_header->looptype;	// For some reason, this is already in the required format.
	  sample_info->samplerate = be32toh (elektron_sample_header->samplerate);	//In the case of the RAW filesystem is not used and it is harmless.
	  sample_info->channels = 1;
	  sample_info->bitdepth = 16;
	  control->data = sample_info;
	  debug_print (2, "Loop start at %d, loop end at %d\n",
		       sample_info->loopstart, sample_info->loopend);
	}

      free_msg (rx_msg);

      set_job_control_progress (control, next_block_start / (double) frames);
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);

      usleep (BE_REST_TIME_US);
    }

  debug_print (2, "%d bytes received\n", next_block_start);

  if (active)
    {
      copy_array (array, output);
    }
  else
    {
      res = -1;
    }

  tx_msg = new_msg_close_read (id);
  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      res = -EIO;
      goto cleanup;
    }
  //Response: x, x, x, x, 0xb1, 00 00 00 0a 00 01 65 de (sample id and received bytes)
  free_msg (rx_msg);

cleanup:
  free_msg (array);
  if (res)
    {
      g_free (control->data);
    }
  return res;
}

static gint
elektron_download_sample_part (struct backend *backend, const gchar * path,
			       GByteArray * output,
			       struct job_control *control)
{
  return elektron_download_smplrw (backend, path, output, control,
				   elektron_new_msg_open_sample_read,
				   sizeof (struct elektron_sample_header),
				   elektron_new_msg_read_sample_blk,
				   elektron_new_msg_close_sample_read,
				   elektron_copy_sample_data);
}

static gint
elektron_download_sample (struct backend *backend, const gchar * path,
			  GByteArray * output, struct job_control *control)
{
  control->parts = 1;
  control->part = 0;
  return elektron_download_sample_part (backend, path, output, control);
}

static gint
elektron_download_raw (struct backend *backend, const gchar * path,
		       GByteArray * output, struct job_control *control)
{
  gint ret;
  gchar *path_with_ext = elektron_add_ext_to_mc_snd (path);
  ret = elektron_download_smplrw (backend, path_with_ext, output, control,
				  elektron_new_msg_open_raw_read,
				  0, elektron_new_msg_read_raw_blk,
				  elektron_new_msg_close_raw_read,
				  elektron_copy_raw_data);
  g_free (path_with_ext);
  return ret;
}

static GByteArray *
elektron_new_msg_upgrade_os_start (guint size)
{
  GByteArray *msg = elektron_new_msg (OS_UPGRADE_START_REQUEST,
				      sizeof (OS_UPGRADE_START_REQUEST));

  memcpy (&msg->data[5], &size, sizeof (guint32));

  return msg;
}

static GByteArray *
elektron_new_msg_upgrade_os_write (GByteArray * os_data, gint * offset)
{
  GByteArray *msg = elektron_new_msg (OS_UPGRADE_WRITE_RESPONSE,
				      sizeof (OS_UPGRADE_WRITE_RESPONSE));
  guint len;
  guint32 crc;
  guint32 aux32;

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
  memcpy (&msg->data[5], &aux32, sizeof (guint32));
  aux32 = htobe32 (len);
  memcpy (&msg->data[9], &aux32, sizeof (guint32));
  aux32 = htobe32 (*offset);
  memcpy (&msg->data[13], &aux32, sizeof (guint32));

  g_byte_array_append (msg, &os_data->data[*offset], len);

  *offset = *offset + len;

  return msg;
}

static gint
elektron_upgrade_os (struct backend *backend, struct sysex_transfer *transfer)
{
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gint8 op;
  gint offset;
  gint res = 0;
  gboolean active;

  tx_msg = elektron_new_msg_upgrade_os_start (transfer->raw->len);
  rx_msg = elektron_tx_and_rx (backend, tx_msg);

  if (!rx_msg)
    {
      res = -EIO;
      goto end;
    }
  //Response: x, x, x, x, 0xd0, [0 (ok), 1 (error)]...
  op = elektron_get_msg_status (rx_msg);
  if (op)
    {
      res = -EIO;
      error_print ("%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      goto end;
    }

  free_msg (rx_msg);

  offset = 0;
  while (offset < transfer->raw->len)
    {
      tx_msg = elektron_new_msg_upgrade_os_write (transfer->raw, &offset);
      rx_msg = elektron_tx_and_rx (backend, tx_msg);

      if (!rx_msg)
	{
	  res = -EIO;
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
	  res = -EIO;
	  error_print ("%s (%s)\n", backend_strerror (backend, res),
		       elektron_get_msg_string (rx_msg));
	  free_msg (rx_msg);
	  break;
	}

      free_msg (rx_msg);

      usleep (BE_REST_TIME_US);

      g_mutex_lock (&transfer->mutex);
      active = transfer->active;
      g_mutex_unlock (&transfer->mutex);
      if (!active)
	{
	  res = -ECANCELED;
	  goto end;
	}
    }

end:
  return res;
}

static gint
elektron_get_storage_stats (struct backend *backend, gint type,
			    struct backend_storage_stats *statfs)
{
  GByteArray *tx_msg, *rx_msg;
  gint8 op;
  guint64 *data;
  int index;
  gint res = 0;

  tx_msg = elektron_new_msg_uint8 (STORAGEINFO_REQUEST,
				   sizeof (STORAGEINFO_REQUEST), type);
  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }

  op = elektron_get_msg_status (rx_msg);
  if (!op)
    {
      error_print ("%s (%s)\n", backend_strerror (backend, -EIO),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return -EIO;
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
  data = (guint64 *) & rx_msg->data[6];
  statfs->bfree = be64toh (*data);
  data = (guint64 *) & rx_msg->data[14];
  statfs->bsize = be64toh (*data);

  free_msg (rx_msg);

  return res;
}

static gint
elektron_configure_device (struct backend *backend, guint8 id)
{
  gint err, devices;
  JsonParser *parser;
  JsonReader *reader;
  gchar *devices_filename;
  GError *error = NULL;
  const gchar *elektroid_elektron_json;
  struct elektron_data *data = backend->data;

  parser = json_parser_new ();

  elektroid_elektron_json = getenv ("ELEKTROID_ELEKTRON_JSON");

  if (elektroid_elektron_json)
    {
      devices_filename = strdup (elektroid_elektron_json);
    }
  else
    {
      devices_filename = get_expanded_dir (CONF_DIR DEVICES_FILE);
    }

  if (!json_parser_load_from_file (parser, devices_filename, &error))
    {
      debug_print (1, "%s\n", error->message);
      g_clear_error (&error);

      g_free (devices_filename);
      devices_filename = strdup (DATADIR DEVICES_FILE);

      debug_print (1, "Falling back to %s...\n", devices_filename);

      if (!json_parser_load_from_file (parser, devices_filename, &error))
	{
	  error_print ("%s", error->message);
	  g_clear_error (&error);
	  err = -ENODEV;
	  goto cleanup_parser;
	}
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser");
      err = -ENODEV;
      goto cleanup_parser;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Not an array\n");
      err = -ENODEV;
      goto cleanup_reader;
    }

  devices = json_reader_count_elements (reader);
  if (!devices)
    {
      debug_print (1, "No devices found\n");
      err = -ENODEV;
      goto cleanup_reader;
    }

  err = -ENODEV;
  for (int i = 0; i < devices; i++)
    {
      if (!json_reader_read_element (reader, i))
	{
	  error_print ("Cannot read element %d. Continuing...\n", i);
	  continue;
	}

      if (!json_reader_read_member (reader, DEV_TAG_ID))
	{
	  error_print ("Cannot read member '%s'. Continuing...\n",
		       DEV_TAG_ID);
	  continue;
	}
      data->device_desc.id = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (data->device_desc.id != id)
	{
	  json_reader_end_element (reader);
	  continue;
	}

      err = 0;
      debug_print (1, "Device %d found\n", id);

      if (!json_reader_read_member (reader, DEV_TAG_ALIAS))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_ALIAS);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      snprintf (data->device_desc.alias, LABEL_MAX, "%s",
		json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_NAME))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_NAME);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      snprintf (backend->name, LABEL_MAX, "%s",
		json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_FILESYSTEMS))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_FILESYSTEMS);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      backend->filesystems = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_STORAGE))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_STORAGE);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      backend->storage = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      break;
    }

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  g_free (devices_filename);
  if (err)
    {
      data->device_desc.id = -1;
    }
  return err;
}

GByteArray *
elektron_ping (struct backend *backend)
{
  GByteArray *tx_msg, *rx_msg;
  struct elektron_data *data = g_malloc (sizeof (struct elektron_data));

  data->seq = 0;
  backend->data = data;

  tx_msg = elektron_new_msg (PING_REQUEST, sizeof (PING_REQUEST));
  rx_msg = elektron_tx_and_rx_timeout (backend, tx_msg,
				       BE_SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      backend->data = NULL;
      g_free (data);
    }


  return rx_msg;
}

gint
elektron_handshake (struct backend *backend)
{
  guint8 id;
  gchar *overbridge_name;
  GByteArray *tx_msg, *rx_msg;
  struct elektron_data *data;

  rx_msg = elektron_ping (backend);
  if (!rx_msg)
    {
      return -ENODEV;
    }

  data = backend->data;

  overbridge_name = strdup ((gchar *) & rx_msg->data[7 + rx_msg->data[6]]);
  id = rx_msg->data[5];
  free_msg (rx_msg);

  if (elektron_configure_device (backend, id))
    {
      backend->data = NULL;
      g_free (overbridge_name);
      g_free (data);
      return -ENODEV;
    }

  tx_msg = elektron_new_msg (SOFTWARE_VERSION_REQUEST,
			     sizeof (SOFTWARE_VERSION_REQUEST));
  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      backend->data = NULL;
      g_free (overbridge_name);
      g_free (data);
      return -ENODEV;
    }
  snprintf (backend->version, LABEL_MAX, "%s", (gchar *) & rx_msg->data[10]);
  free_msg (rx_msg);

  if (debug_level > 1)
    {
      tx_msg = elektron_new_msg (DEVICEUID_REQUEST,
				 sizeof (DEVICEUID_REQUEST));
      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (rx_msg)
	{
	  debug_print (1, "UID: %x\n", *((guint32 *) & rx_msg->data[5]));
	  free_msg (rx_msg);
	}
    }

  snprintf (backend->description, LABEL_MAX, "%s", overbridge_name);

  g_free (overbridge_name);

  backend->fs_ops = FS_OPERATIONS;
  backend->destroy_data = backend_destroy_data;
  backend->upgrade_os = elektron_upgrade_os;
  backend->get_storage_stats = elektron_get_storage_stats;

  return 0;
}

static guint
elektron_next_data_entry (struct item_iterator *iter)
{
  gchar *name_cp1252;
  guint32 *data32;
  guint16 *data16;
  guint8 type;
  guint8 has_children;
  guint32 id;
  struct elektron_iterator_data *data = iter->data;

  if (data->pos == data->msg->len)
    {
      //A data directory only contains either files or directories.
      //If the last visited item was a directory, there are no more slots to visit.
      if (iter->item.type == ELEKTROID_DIR
	  || iter->item.id >= data->max_slots)
	{
	  return -ENOENT;
	}

      goto not_found;
    }

  name_cp1252 = (gchar *) & data->msg->data[data->pos];

  if (data->max_slots != -1 && iter->item.type != ELEKTROID_DIR)
    {
      data32 =
	(guint32 *) & data->msg->data[data->pos + strlen (name_cp1252) + 3];
      id = be32toh (*data32);
      if (id > iter->item.id + 1)
	{
	  goto not_found;
	}
    }

  elektron_get_utf8 (iter->item.name, name_cp1252);
  data->pos += strlen (name_cp1252) + 1;
  has_children = data->msg->data[data->pos];
  data->pos++;
  type = data->msg->data[data->pos];
  data->pos++;

  switch (type)
    {
    case 1:
      iter->item.type = ELEKTROID_DIR;
      data->pos += sizeof (guint32);	// child entries
      iter->item.size = 0;
      iter->item.id = -1;
      data->operations = 0;
      data->has_valid_data = 0;
      data->has_metadata = 0;
      break;
    case 2:
      iter->item.type = has_children ? ELEKTROID_DIR : ELEKTROID_FILE;

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->item.id = has_children ? -1 : be32toh (*data32);
      data->pos += sizeof (gint32);

      data32 = (guint32 *) & data->msg->data[data->pos];
      iter->item.size = be32toh (*data32);
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
      error_print ("Unrecognized data entry: %d\n", iter->item.type);
      break;
    }

  return 0;

not_found:
  iter->item.type = ELEKTROID_FILE;
  iter->item.name[0] = 0;
  iter->item.size = -1;
  iter->item.id++;
  data->operations = 0;
  data->has_valid_data = 0;
  data->has_metadata = 0;
  return 0;
}

static gchar *
elektron_add_prefix_to_path (const gchar * dir, const gchar * prefix)
{
  gchar *full;

  if (prefix)
    {
      GString *str = g_string_new (NULL);
      g_string_append_printf (str, "%s%s", prefix, dir);
      full = str->str;
      g_string_free (str, FALSE);
    }
  else
    {
      full = strdup (dir);
    }

  return full;
}

static gint
elektron_read_data_dir_prefix (struct backend *backend,
			       struct item_iterator *iter,
			       const gchar * dir, const char *prefix,
			       gint32 max_slots)
{
  int res;
  GByteArray *tx_msg;
  GByteArray *rx_msg;
  gchar *dir_w_prefix = elektron_add_prefix_to_path (dir, prefix);

  tx_msg = elektron_new_msg_list (dir_w_prefix, 0, 0, 1);
  g_free (dir_w_prefix);
  if (!tx_msg)
    {
      return -EINVAL;
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }

  res = elektron_get_msg_status (rx_msg);
  if (!res)
    {
      free_msg (rx_msg);
      return -ENOTDIR;
    }

  return elektron_init_iterator (iter, rx_msg, elektron_next_data_entry,
				 FS_DATA_ALL, max_slots);
}

static gint
elektron_read_data_dir_any (struct backend *backend,
			    struct item_iterator *iter, const gchar * dir)
{
  return elektron_read_data_dir_prefix (backend, iter, dir, NULL, -1);
}

static gint
elektron_read_data_dir_prj (struct backend *backend,
			    struct item_iterator *iter, const gchar * dir)
{
  return elektron_read_data_dir_prefix (backend, iter, dir,
					FS_DATA_PRJ_PREFIX, 128);
}

static gint
elektron_read_data_dir_snd (struct backend *backend,
			    struct item_iterator *iter, const gchar * dir)
{
  return elektron_read_data_dir_prefix (backend, iter, dir,
					FS_DATA_SND_PREFIX, 256);
}

static gint
elektron_dst_src_data_prefix_common (struct backend *backend,
				     const gchar * src, const gchar * dst,
				     const char *prefix,
				     const guint8 * op_data, guint len)
{
  gint res;
  char *src_w_prefix = elektron_add_prefix_to_path (src, prefix);
  char *dst_w_prefix = elektron_add_prefix_to_path (dst, prefix);

  res = elektron_src_dst_common (backend, src_w_prefix, dst_w_prefix,
				 op_data, len);
  g_free (src_w_prefix);
  g_free (dst_w_prefix);

  return res;
}

static gint
elektron_move_data_item_prefix (struct backend *backend, const gchar * src,
				const gchar * dst, const char *prefix)
{
  return elektron_dst_src_data_prefix_common (backend, src, dst, prefix,
					      DATA_MOVE_REQUEST,
					      sizeof (DATA_MOVE_REQUEST));
}

static gint
elektron_move_data_item_any (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_move_data_item_prefix (backend, src, dst, NULL);
}

static gint
elektron_move_data_item_prj (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_move_data_item_prefix (backend, src, dst,
					 FS_DATA_PRJ_PREFIX);
}

static gint
elektron_move_data_item_snd (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_move_data_item_prefix (backend, src, dst,
					 FS_DATA_SND_PREFIX);
}

static gint
elektron_copy_data_item_prefix (struct backend *backend, const gchar * src,
				const gchar * dst, const gchar * prefix)
{
  return elektron_dst_src_data_prefix_common (backend, src, dst, prefix,
					      DATA_COPY_REQUEST,
					      sizeof (DATA_COPY_REQUEST));
}

static gint
elektron_copy_data_item_any (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_copy_data_item_prefix (backend, src, dst, NULL);
}

static gint
elektron_copy_data_item_prj (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_copy_data_item_prefix (backend, src, dst,
					 FS_DATA_PRJ_PREFIX);
}

static gint
elektron_copy_data_item_snd (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_copy_data_item_prefix (backend, src, dst,
					 FS_DATA_SND_PREFIX);
}

static gint
elektron_path_data_prefix_common (struct backend *backend,
				  const gchar * path, const char *prefix,
				  const guint8 * op_data, guint len)
{
  gint res;
  char *path_w_prefix = elektron_add_prefix_to_path (path, prefix);

  res = elektron_path_common (backend, path_w_prefix, op_data, len);
  g_free (path_w_prefix);

  return res;
}

static gint
elektron_clear_data_item_prefix (struct backend *backend,
				 const gchar * path, const gchar * prefix)
{
  return elektron_path_data_prefix_common (backend, path, prefix,
					   DATA_CLEAR_REQUEST,
					   sizeof (DATA_CLEAR_REQUEST));
}

static gint
elektron_clear_data_item_any (struct backend *backend, const gchar * path)
{
  return elektron_clear_data_item_prefix (backend, path, NULL);
}

static gint
elektron_clear_data_item_prj (struct backend *backend, const gchar * path)
{
  return elektron_clear_data_item_prefix (backend, path, FS_DATA_PRJ_PREFIX);
}

static gint
elektron_clear_data_item_snd (struct backend *backend, const gchar * path)
{
  return elektron_clear_data_item_prefix (backend, path, FS_DATA_SND_PREFIX);
}

static gint
elektron_swap_data_item_prefix (struct backend *backend, const gchar * src,
				const gchar * dst, const gchar * prefix)
{
  return elektron_dst_src_data_prefix_common (backend, src, dst, prefix,
					      DATA_SWAP_REQUEST,
					      sizeof (DATA_SWAP_REQUEST));
}

static gint
elektron_swap_data_item_any (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_swap_data_item_prefix (backend, src, dst, NULL);
}

static gint
elektron_swap_data_item_prj (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_swap_data_item_prefix (backend, src, dst,
					 FS_DATA_PRJ_PREFIX);
}

static gint
elektron_swap_data_item_snd (struct backend *backend, const gchar * src,
			     const gchar * dst)
{
  return elektron_swap_data_item_prefix (backend, src, dst,
					 FS_DATA_SND_PREFIX);
}

static gint
elektron_open_datum (struct backend *backend, const gchar * path,
		     guint32 * jid, gint mode, guint32 size)
{
  guint32 *data32;
  guint32 sizebe;
  guint32 chunk_size;
  guint8 compression;
  GByteArray *rx_msg;
  GByteArray *tx_msg;
  const guint8 *data;
  guint len;
  gchar *path_cp1252;
  gint res = 0;

  if (mode == O_RDONLY)
    {
      data = DATA_READ_OPEN_REQUEST;
      len = sizeof (DATA_READ_OPEN_REQUEST);
    }
  else if (mode == O_WRONLY)
    {
      data = DATA_WRITE_OPEN_REQUEST;
      len = sizeof (DATA_WRITE_OPEN_REQUEST);
    }
  else
    {
      return -EINVAL;
    }

  tx_msg = elektron_new_msg (data, len);
  if (!tx_msg)
    {
      return -ENOMEM;
    }

  path_cp1252 = elektron_get_cp1252 (path);

  if (mode == O_RDONLY)
    {
      g_byte_array_append (tx_msg, (guint8 *) path_cp1252,
			   strlen (path_cp1252) + 1);
      chunk_size = htobe32 (DATA_TRANSF_BLOCK_BYTES);
      g_byte_array_append (tx_msg, (guint8 *) & chunk_size, sizeof (guint32));
      compression = 1;
      g_byte_array_append (tx_msg, &compression, sizeof (guint8));
    }

  if (mode == O_WRONLY)
    {
      sizebe = htobe32 (size);
      g_byte_array_append (tx_msg, (guint8 *) & sizebe, sizeof (guint32));
      g_byte_array_append (tx_msg, (guint8 *) path_cp1252,
			   strlen (path_cp1252) + 1);
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      res = -EIO;
      goto cleanup;
    }

  if (!elektron_get_msg_status (rx_msg))
    {
      res = -EPERM;
      error_print ("%s (%s)\n", backend_strerror (backend, res),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      goto cleanup;
    }

  data32 = (guint32 *) & rx_msg->data[6];
  *jid = be32toh (*data32);

  if (mode == O_RDONLY)
    {
      data32 = (guint32 *) & rx_msg->data[10];
      chunk_size = be32toh (*data32);

      compression = rx_msg->data[14];

      debug_print (1,
		   "Open datum info: job id: %d; chunk size: %d; compression: %d\n",
		   *jid, chunk_size, compression);
    }

  if (mode == O_WRONLY)
    {
      debug_print (1, "Open datum info: job id: %d\n", *jid);
    }

  free_msg (rx_msg);

cleanup:
  g_free (path_cp1252);
  return res;
}

static gint
elektron_close_datum (struct backend *backend,
		      guint32 jid, gint mode, guint32 wsize)
{
  guint32 jidbe;
  guint32 wsizebe;
  guint32 r_jid;
  guint32 asize;
  guint32 *data32;
  GByteArray *rx_msg;
  GByteArray *tx_msg;
  const guint8 *data;
  guint len;

  if (mode == O_RDONLY)
    {
      data = DATA_READ_CLOSE_REQUEST;
      len = sizeof (DATA_READ_CLOSE_REQUEST);
    }
  else if (mode == O_WRONLY)
    {
      data = DATA_WRITE_CLOSE_REQUEST;
      len = sizeof (DATA_WRITE_CLOSE_REQUEST);
    }
  else
    {
      return -EINVAL;
    }

  tx_msg = elektron_new_msg (data, len);
  if (!tx_msg)
    {
      return -ENOMEM;
    }

  jidbe = htobe32 (jid);
  g_byte_array_append (tx_msg, (guchar *) & jidbe, sizeof (guint32));

  if (mode == O_WRONLY)
    {
      wsizebe = htobe32 (wsize);
      g_byte_array_append (tx_msg, (guchar *) & wsizebe, sizeof (guint32));
    }

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return -EIO;
    }

  if (!elektron_get_msg_status (rx_msg))
    {
      error_print ("%s (%s)\n", backend_strerror (backend, -EPERM),
		   elektron_get_msg_string (rx_msg));
      free_msg (rx_msg);
      return -EPERM;
    }

  data32 = (guint32 *) & rx_msg->data[6];
  r_jid = be32toh (*data32);

  data32 = (guint32 *) & rx_msg->data[10];
  asize = be32toh (*data32);

  debug_print (1, "Close datum info: job id: %d; size: %d\n", r_jid, asize);

  free_msg (rx_msg);

  if (mode == O_WRONLY && asize != wsize)
    {
      error_print
	("Actual download bytes (%d) differs from expected ones (%d)\n",
	 asize, wsize);
      return -EINVAL;
    }

  return 0;
}

static gint
elektron_download_data_prefix (struct backend *backend, const gchar * path,
			       GByteArray * output,
			       struct job_control *control,
			       const gchar * prefix)
{
  gint res;
  guint32 seq;
  guint32 seqbe;
  guint32 jid;
  guint32 r_jid;
  guint32 r_seq;
  guint32 status;
  guint8 last;
  guint32 hash;
  guint32 *data32;
  guint32 jidbe;
  guint32 data_size;
  gboolean active;
  GByteArray *rx_msg;
  GByteArray *tx_msg;
  gchar *path_w_prefix = elektron_add_prefix_to_path (path, prefix);

  res = elektron_open_datum (backend, path_w_prefix, &jid, O_RDONLY, 0);
  g_free (path_w_prefix);
  if (res)
    {
      return -EIO;
    }

  usleep (BE_REST_TIME_US);

  jidbe = htobe32 (jid);

  res = 0;
  seq = 0;
  last = 0;
  control->data = NULL;
  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);
  while (!last && active)
    {
      tx_msg =
	elektron_new_msg (DATA_READ_PARTIAL_REQUEST,
			  sizeof (DATA_READ_PARTIAL_REQUEST));
      g_byte_array_append (tx_msg, (guint8 *) & jidbe, sizeof (guint32));
      seqbe = htobe32 (seq);
      g_byte_array_append (tx_msg, (guint8 *) & seqbe, sizeof (guint32));
      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (!rx_msg)
	{
	  res = -EIO;
	  break;
	}

      if (!elektron_get_msg_status (rx_msg))
	{
	  res = -EPERM;
	  error_print ("%s (%s)\n", backend_strerror (backend, res),
		       elektron_get_msg_string (rx_msg));
	  free_msg (rx_msg);
	  break;
	}

      data32 = (guint32 *) & rx_msg->data[6];
      r_jid = be32toh (*data32);

      data32 = (guint32 *) & rx_msg->data[10];
      r_seq = be32toh (*data32);

      data32 = (guint32 *) & rx_msg->data[14];
      status = be32toh (*data32);

      last = rx_msg->data[18];

      data32 = (guint32 *) & rx_msg->data[19];
      hash = be32toh (*data32);

      data32 = (guint32 *) & rx_msg->data[23];
      data_size = be32toh (*data32);

      if (data_size)
	{
	  debug_print (1,
		       "Read datum info: job id: %d; last: %d; seq: %d; status: %d; hash: 0x%08x\n",
		       r_jid, last, r_seq, status, hash);

	  g_byte_array_append (output, (guint8 *) & rx_msg->data[27],
			       data_size);
	}
      else
	{
	  // Sometimes, the first message returns 0 data size and the rest of the parameters are not initialized.
	  debug_print (1,
		       "Read datum info: job id: %d; last: %d, hash: 0x%08x\n",
		       r_jid, last, hash);
	  status = 0;
	}

      free_msg (rx_msg);
      seq++;

      if (control)
	{
	  set_job_control_progress (control, status / 1000.0);
	  g_mutex_lock (&control->mutex);
	  active = control->active;
	  g_mutex_unlock (&control->mutex);
	}

      usleep (BE_REST_TIME_US);
    }

  return elektron_close_datum (backend, jid, O_RDONLY, 0);
}

static gint
elektron_download_data_any (struct backend *backend, const gchar * path,
			    GByteArray * output, struct job_control *control)
{
  control->parts = 1;
  control->part = 0;
  return elektron_download_data_prefix (backend, path, output, control, NULL);
}

static gint
elektron_download_data_prj (struct backend *backend, const gchar * path,
			    GByteArray * output, struct job_control *control)
{
  return elektron_download_data_prefix (backend, path, output, control,
					FS_DATA_PRJ_PREFIX);
}

static gint
elektron_download_data_snd (struct backend *backend, const gchar * path,
			    GByteArray * output, struct job_control *control)
{
  return elektron_download_data_prefix (backend, path, output, control,
					FS_DATA_SND_PREFIX);
}

static gchar *
elektron_get_download_name (struct backend *backend,
			    const struct fs_operations *ops,
			    const gchar * src_path)
{
  gint32 id;
  gint ret;
  gchar *dir, *name;
  struct item_iterator *iter;

  if (ops->fs == FS_SAMPLES || ops->fs == FS_RAW_ALL
      || ops->fs == FS_RAW_PRESETS)
    {
      return g_path_get_basename (src_path);
    }

  iter = malloc (sizeof (struct item_iterator));
  dir = g_path_get_dirname (src_path);
  ret = ops->readdir (backend, iter, dir);
  g_free (dir);
  if (ret)
    {
      return NULL;
    }

  name = g_path_get_basename (src_path);
  id = atoi (name);
  g_free (name);

  while (!next_item_iterator (iter))
    {
      if (iter->item.id == id)
	{
	  name = g_strdup (iter->item.name);
	  break;
	}
    }

  free_item_iterator (iter);

  return name;
}

static gint
elektron_download_pkg (struct backend *backend, const gchar * path,
		       GByteArray * output, struct job_control *control,
		       enum package_type type,
		       const struct fs_operations *ops,
		       fs_remote_file_op download)
{
  gint ret;
  gchar *pkg_name;
  struct package pkg;
  struct elektron_data *data = backend->data;

  pkg_name = elektron_get_download_name (backend, ops, path);
  if (!pkg_name)
    {
      return -1;
    }

  if (package_begin (&pkg, pkg_name, backend->version, &data->device_desc,
		     type))
    {
      g_free (pkg_name);
      return -1;
    }

  ret = package_receive_pkg_resources (&pkg, path, control, backend, download,
				       elektron_download_sample_part);
  ret = ret || package_end (&pkg, output);

  package_destroy (&pkg);
  return ret;
}

static gint
elektron_download_data_snd_pkg (struct backend *backend,
				const gchar * path, GByteArray * output,
				struct job_control *control)
{
  return elektron_download_pkg (backend, path, output, control,
				PKG_FILE_TYPE_SOUND,
				&FS_DATA_SND_OPERATIONS,
				elektron_download_data_snd);
}

static gint
elektron_download_data_prj_pkg (struct backend *backend,
				const gchar * path, GByteArray * output,
				struct job_control *control)
{
  return elektron_download_pkg (backend, path, output, control,
				PKG_FILE_TYPE_PROJECT,
				&FS_DATA_PRJ_OPERATIONS,
				elektron_download_data_prj);
}

static gint
elektron_download_raw_pst_pkg (struct backend *backend, const gchar * path,
			       GByteArray * output,
			       struct job_control *control)
{
  return elektron_download_pkg (backend, path, output, control,
				PKG_FILE_TYPE_PRESET,
				&FS_RAW_ANY_OPERATIONS,
				elektron_download_raw);
}

static gchar *
elektron_get_upload_path_smplrw (struct backend *backend,
				 const struct fs_operations *ops,
				 const gchar * dst_dir,
				 const gchar * src_path)
{
  gchar *path, *name, *aux;

  name = g_path_get_basename (src_path);
  remove_ext (name);
  aux = chain_path (dst_dir, name);
  g_free (name);

  if (ops->fs == FS_RAW_ALL || ops->fs == FS_RAW_PRESETS)
    {
      path = elektron_add_ext_to_mc_snd (aux);
      g_free (aux);
    }
  else
    {
      path = aux;
    }
  return path;
}

// As Elektron devices provide their own file extension and the file content is
// not just SysEx, it is not needed to indicate in the filename the type of
// device, filesystem or id.

static gchar *
elektron_get_download_path (struct backend *backend,
			    const struct fs_operations *ops,
			    const gchar * dst_dir, const gchar * src_path,
			    GByteArray * data)
{
  gchar *path, *name, *dl_ext, *src_fpath;
  const gchar *md_ext, *ext = get_ext (src_path);

  // Examples:
  // 0:/project0
  // 0:/soundbanks/A/1
  // 0:/soundbanks/A/1/.metadata
  // 0:/loops/sample

  if (ext && strcmp (ext, "metadata") == 0)
    {
      src_fpath = g_path_get_dirname (src_path);
      md_ext = ".metadata";
    }
  else
    {
      src_fpath = strdup (src_path);
      md_ext = "";
    }

  name = elektron_get_download_name (backend, ops, src_fpath);
  g_free (src_fpath);
  if (name)
    {
      GString *filename = g_string_new (NULL);
      dl_ext = ops->get_ext (backend, ops);
      g_string_append_printf (filename, "%s.%s%s", name, dl_ext, md_ext);
      path = chain_path (dst_dir, filename->str);
      g_free (name);
      g_free (dl_ext);
      g_string_free (filename, TRUE);
    }
  else
    {
      path = NULL;
    }

  return path;
}

static void
elektron_remove_slot_name_from_path (gchar * path)
{
  gchar *c = path + strlen (path) - 1;
  while (*c != ':')
    {
      c--;
    }
  *c = 0;
}

static gint
elektron_upload_data_prefix (struct backend *backend, const gchar * path,
			     GByteArray * array,
			     struct job_control *control,
			     const gchar * prefix)
{
  gint res;
  guint32 seq;
  guint32 jid;
  guint32 crc;
  guint32 len;
  guint32 r_jid;
  guint32 r_seq;
  guint32 offset;
  guint32 *data32;
  guint32 jidbe;
  guint32 aux32;
  gboolean active;
  guint32 total;
  GByteArray *rx_msg;
  GByteArray *tx_msg;
  gchar *path_w_prefix = elektron_add_prefix_to_path (path, prefix);
  elektron_remove_slot_name_from_path (path_w_prefix);	//The slot name is not used with Elektron devices

  res = elektron_open_datum (backend, path_w_prefix, &jid, O_WRONLY,
			     array->len);
  g_free (path_w_prefix);
  if (res)
    {
      goto end;
    }

  usleep (BE_REST_TIME_US);

  jidbe = htobe32 (jid);

  seq = 0;
  offset = 0;
  control->data = NULL;
  if (control)
    {
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);
    }
  else
    {
      active = TRUE;
    }

  while (offset < array->len && active)
    {
      tx_msg =
	elektron_new_msg (DATA_WRITE_PARTIAL_REQUEST,
			  sizeof (DATA_WRITE_PARTIAL_REQUEST));
      g_byte_array_append (tx_msg, (guint8 *) & jidbe, sizeof (guint32));
      aux32 = htobe32 (seq);
      g_byte_array_append (tx_msg, (guint8 *) & aux32, sizeof (guint32));

      if (offset + DATA_TRANSF_BLOCK_BYTES < array->len)
	{
	  len = DATA_TRANSF_BLOCK_BYTES;
	}
      else
	{
	  len = array->len - offset;
	}

      crc = crc32 (0xffffffff, &array->data[offset], len);
      aux32 = htobe32 (crc);
      g_byte_array_append (tx_msg, (guint8 *) & aux32, sizeof (guint32));

      aux32 = htobe32 (len);
      g_byte_array_append (tx_msg, (guint8 *) & aux32, sizeof (guint32));

      g_byte_array_append (tx_msg, &array->data[offset], len);

      rx_msg = elektron_tx_and_rx (backend, tx_msg);
      if (!rx_msg)
	{
	  res = -EIO;
	  goto end;
	}

      usleep (BE_REST_TIME_US);

      if (!elektron_get_msg_status (rx_msg))
	{
	  res = -EPERM;
	  error_print ("%s (%s)\n", backend_strerror (backend, res),
		       elektron_get_msg_string (rx_msg));
	  free_msg (rx_msg);
	  break;
	}

      data32 = (guint32 *) & rx_msg->data[6];
      r_jid = be32toh (*data32);

      data32 = (guint32 *) & rx_msg->data[10];
      r_seq = be32toh (*data32);

      data32 = (guint32 *) & rx_msg->data[14];
      total = be32toh (*data32);

      free_msg (rx_msg);

      debug_print (1,
		   "Write datum info: job id: %d; seq: %d; total: %d\n",
		   r_jid, r_seq, total);

      seq++;
      offset += len;

      if (total != offset)
	{
	  error_print
	    ("Actual upload bytes (%d) differs from expected ones (%d)\n",
	     total, offset);
	}

      set_job_control_progress (control, offset / (gdouble) array->len);
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);
    }

  debug_print (2, "%d bytes sent\n", offset);

  res = elektron_close_datum (backend, jid, O_WRONLY, array->len);

end:
  return res;
}

static gint
elektron_upload_data_any (struct backend *backend, const gchar * path,
			  GByteArray * array, struct job_control *control)
{
  control->parts = 1;
  control->part = 0;
  return elektron_upload_data_prefix (backend, path, array, control, NULL);
}

static gint
elektron_upload_data_prj (struct backend *backend, const gchar * path,
			  GByteArray * array, struct job_control *control)
{
  return elektron_upload_data_prefix (backend, path, array, control,
				      FS_DATA_PRJ_PREFIX);
}

static gint
elektron_upload_data_snd (struct backend *backend, const gchar * path,
			  GByteArray * array, struct job_control *control)
{
  return elektron_upload_data_prefix (backend, path, array, control,
				      FS_DATA_SND_PREFIX);
}

static gint
elektron_upload_pkg (struct backend *backend, const gchar * path,
		     GByteArray * input, struct job_control *control,
		     guint8 type, const struct fs_operations *ops,
		     fs_remote_file_op upload)
{
  gint ret;
  struct package pkg;
  struct elektron_data *data = backend->data;

  ret = package_open (&pkg, input, &data->device_desc);
  if (!ret)
    {
      ret = package_send_pkg_resources (&pkg, path, control, backend, upload);
      package_close (&pkg);
    }
  return ret;
}

static gint
elektron_upload_data_snd_pkg (struct backend *backend, const gchar * path,
			      GByteArray * input, struct job_control *control)
{
  return elektron_upload_pkg (backend, path, input, control,
			      PKG_FILE_TYPE_SOUND,
			      &FS_DATA_SND_OPERATIONS,
			      elektron_upload_data_snd);
}

static gint
elektron_upload_data_prj_pkg (struct backend *backend, const gchar * path,
			      GByteArray * input, struct job_control *control)
{
  return elektron_upload_pkg (backend, path, input, control,
			      PKG_FILE_TYPE_PROJECT,
			      &FS_DATA_PRJ_OPERATIONS,
			      elektron_upload_data_prj);
}

static gint
elektron_upload_raw_pst_pkg (struct backend *backend, const gchar * path,
			     GByteArray * input, struct job_control *control)
{
  return elektron_upload_pkg (backend, path, input, control,
			      PKG_FILE_TYPE_PRESET,
			      &FS_RAW_ANY_OPERATIONS, elektron_upload_raw);
}

static gchar *
elektron_get_dev_and_fs_ext (struct backend *backend,
			     const struct fs_operations *ops)
{
  struct elektron_data *data = backend->data;
  gchar *ext = malloc (LABEL_MAX);
  snprintf (ext, LABEL_MAX, "%s%s", data->device_desc.alias, ops->type_ext);
  return ext;
}

gint
elektron_sample_load (const gchar * path, GByteArray * sample,
		      struct job_control *control)
{
  guint frames;
  return sample_load_from_file (path, sample, control,
				&ELEKTRON_SAMPLE_PARAMS, &frames);
}

gchar *
elektron_get_sample_path_from_hash_size (struct backend *backend,
					 guint32 hash, guint32 size)
{
  guint32 aux32;
  gchar *path;
  GByteArray *rx_msg, *tx_msg =
    elektron_new_msg (FS_SAMPLE_GET_FILE_INFO_FROM_HASH_AND_SIZE_REQUEST,
		      sizeof
		      (FS_SAMPLE_GET_FILE_INFO_FROM_HASH_AND_SIZE_REQUEST));

  aux32 = htobe32 (hash);
  memcpy (&tx_msg->data[5], &aux32, sizeof (guint32));
  aux32 = htobe32 (size);
  memcpy (&tx_msg->data[9], &aux32, sizeof (guint32));

  rx_msg = elektron_tx_and_rx (backend, tx_msg);
  if (!rx_msg)
    {
      return NULL;
    }

  if (elektron_get_msg_status (rx_msg))
    {
      path = strdup ((gchar *) & rx_msg->data[14]);
    }
  else
    {
      path = NULL;
    }
  g_byte_array_free (rx_msg, TRUE);
  return path;
}
