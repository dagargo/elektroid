/*
 *   local.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#include "local.h"
#include "connectors/system.h"

const struct fs_operations FS_LOCAL_GENERIC_OPERATIONS = {
  .options = FS_OPTION_SORT_BY_NAME | FS_OPTION_ALLOW_SEARCH,
  .name = "local",
  .gui_name = "localhost",
  .gui_icon = BE_FILE_ICON_GENERIC,
  .readdir = system_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .max_name_len = 255
};

const struct fs_operations FS_LOCAL_SAMPLE_OPERATIONS = {
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_STEREO |
    FS_OPTION_SORT_BY_NAME | FS_OPTION_SHOW_SAMPLE_COLUMNS |
    FS_OPTION_ALLOW_SEARCH,
  .name = "local",
  .gui_name = "localhost",
  .gui_icon = BE_FILE_ICON_WAVE,
  .readdir = system_samples_read_dir,
  .file_exists = system_file_exists,
  .mkdir = system_mkdir,
  .delete = system_delete,
  .rename = system_rename,
  .move = system_rename,
  .get_exts = sample_get_sample_extensions,
  .max_name_len = 255
};
