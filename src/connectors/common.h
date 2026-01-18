/*
 *   common.c
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

#include "connector.h"
#include "sample.h"

#define DEFAULT_MAX_NAME_LEN 32

#define COMMON_GET_MIDI_BANK(p) ((p & 0x3f80) >> 7)
#define COMMON_GET_MIDI_PRESET(p) (p & 0x7f)

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 EVENTIDE_ID[] = { 0x1c };
static const guint8 KORG_ID[] = { 0x42 };
static const guint8 MOOG_ID[] = { 0x04 };
static const guint8 NOVATION_ID[] = { 0x0, 0x20, 0x29 };

struct common_simple_read_dir_data
{
  guint32 next;
  guint32 last;
};

const gchar **common_sysex_get_extensions (struct backend *backend,
					   const struct fs_operations *ops);

const gchar **common_get_all_extensions (struct backend *backend,
					 const struct fs_operations *ops);

gchar *common_slot_get_upload_path (struct backend *backend,
				    const struct fs_operations *ops,
				    const gchar * dst_dir,
				    const gchar * src_path,
				    struct idata *idata);

gint common_slot_get_id_from_path (const char *path, guint * id);

gchar *common_get_id_as_slot (struct item *item, struct backend *backend);

gchar *common_get_id_as_slot_padded (struct item *item,
				     struct backend *backend, gint digits);

void common_print_item (struct item_iterator *iter, struct backend *backend,
			const struct fs_operations *fs_ops);

void common_midi_program_change (struct backend *backend, const gchar * dir,
				 struct item *item);

void
common_midi_program_change_int (struct backend *backend, const gchar * dir,
				guint32 program);

gint common_simple_next_dentry (struct item_iterator *iter);

gint common_data_tx (struct backend *backend, GByteArray * msg,
		     struct task_control *control);

gint common_data_tx_and_rx (struct backend *backend, GByteArray * tx_msg,
			    GByteArray ** rx_msg,
			    struct task_control *control);

gint common_data_tx_and_rx_part (struct backend *backend, GByteArray * tx_msg,
				 GByteArray ** rx_msg,
				 struct task_control *control);

gchar *common_slot_get_download_path_id_name_ext (struct backend *backend,
						  const struct fs_operations
						  *ops, const gchar * dst_dir,
						  guint id, guint digits,
						  const gchar * name,
						  const gchar * ext);

gchar *common_slot_get_download_path_id_name (struct backend *backend,
					      const struct fs_operations *ops,
					      const gchar * dst_dir,
					      guint id, guint digits,
					      const gchar * name);

gchar *common_slot_get_download_path (struct backend *backend,
				      const struct fs_operations *ops,
				      const gchar * dst_dir,
				      const gchar * src_path,
				      struct idata *idata, guint digits);

gchar *common_slot_get_download_path_n (struct backend *backend,
					const struct fs_operations *ops,
					const gchar * dst_dir,
					const gchar * src_path,
					struct idata *idata);

gchar *common_slot_get_download_path_nn (struct backend *backend,
					 const struct fs_operations *ops,
					 const gchar * dst_dir,
					 const gchar * src_path,
					 struct idata *idata);

gchar *common_slot_get_download_path_nnn (struct backend *backend,
					  const struct fs_operations
					  *ops, const gchar * dst_dir,
					  const gchar * src_path,
					  struct idata *idata);

void common_to_os_sanitized_name (gchar * name);

gchar *common_get_sanitized_name (const gchar * name, const gchar * alphabet,
				  gchar defchar);

gint common_sample_load (const gchar * path, struct idata *sample,
			 struct task_control *control, guint32 channels,
			 guint32 rate, guint32 format, gboolean tags);

gchar *common_system_get_download_path (struct backend *backend,
					const struct fs_operations *ops,
					const gchar * dst_dir,
					const gchar * src_path,
					struct idata *content);

gchar *common_system_get_upload_path (struct backend *backend,
				      const struct fs_operations *ops,
				      const gchar * dst_dir,
				      const gchar * src_path,
				      struct idata *content);

void common_midi_msg_to_8bit_msg (guint8 * msg_midi, guint8 * msg_8bit,
				  guint input_size);

void common_8bit_msg_to_midi_msg (guint8 * msg_8bit, guint8 * msg_midi,
				  guint input_size);

guint common_8bit_msg_to_midi_msg_size (guint size);

guint common_midi_msg_to_8bit_msg_size (guint size);
