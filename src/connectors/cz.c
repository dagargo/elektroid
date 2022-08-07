/*
 *   cz.c
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

#include "cz.h"

#define CZ101_PROGRAM_LEN 263

static const guint8 CZ_PROGRAM_REQUEST[] =
  { 0xf0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x00, 0x70, 0x31 };

enum cz_fs
{
  FS_PROGRAM_CZ = 1
};

static const struct fs_operations FS_PROGRAM_CZ_OPERATIONS = {
  .fs = FS_PROGRAM_CZ,
  .options =
    FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "cz",
  .gui_name = "Programs",
  .gui_icon = BE_FILE_ICON_SND,
  .readdir = NULL,
  .print_item = NULL,
  .mkdir = NULL,
  .delete = NULL,
  .rename = NULL,
  .move = NULL,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = NULL,
  .upload = NULL,
  .getid = get_item_index,
  .load = load_file,
  .save = save_file,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = NULL,
  .get_download_path = NULL,
  .type_ext = "syx"
};

static GByteArray *
cz_get_program_dump_msg (gint program)
{
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (CZ_PROGRAM_REQUEST));
  g_byte_array_append (tx_msg, CZ_PROGRAM_REQUEST,
		       sizeof (CZ_PROGRAM_REQUEST));
  tx_msg->data[6] = (guint8) program;
  return tx_msg;
}

gint
cz_handshake (struct backend *backend)
{
  gint len;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = cz_get_program_dump_msg (0x60);	//Programmer
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -EIO;
    }
  len = rx_msg->len;
  free_msg (rx_msg);
  if (len == CZ101_PROGRAM_LEN)
    {
      snprintf (backend->device_name, LABEL_MAX, "Casio CZ-101");
      return 0;
    }
  else
    {
      return -1;
    }
}
