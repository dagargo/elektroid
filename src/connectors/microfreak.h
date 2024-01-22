/*
 *   microfreak.h
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#ifndef MICROFREAK_H
#define MICROFREAK_H

#include "backend.h"

#define MICROFREAK_PRESET_HEADER_MSG_LEN 0x23
#define MICROFREAK_PRESET_PARTS 146
#define MICROFREAK_PRESET_PART_LEN 0x20
#define MICROFREAK_PRESET_HEADER "22 serialization::archive 10 0 4 3 174"

struct microfreak_preset
{
  guint8 header[MICROFREAK_PRESET_HEADER_MSG_LEN];
  guint8 part[MICROFREAK_PRESET_PARTS][MICROFREAK_PRESET_PART_LEN];
  guint parts;
};

gint microfreak_handshake (struct backend *);

gint microfreak_serialize_preset (GByteArray * output,
				  struct microfreak_preset *mfp);

gint microfreak_deserialize_preset (struct microfreak_preset *mfp,
				    GByteArray * input);

#endif
