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

#include "utils.h"
#include "microfreak_sample.h"

#define MICROFREAK_PRESET_HEADER_MSG_LEN 0x23
#define MICROFREAK_PRESET_PARTS 146
#define MICROFREAK_PRESET_PART_LEN 0x20
#define MICROFREAK_SAMPLE_NAME_LEN 13
#define MICROFREAK_WAVE_BLK_SHRT 14
#define MICROFREAK_WAVE_BLK_LAST_SHRT 4
#define MICROFREAK_WAVE_BLK_SIZE (MICROFREAK_WAVE_BLK_SHRT * MICROFREAK_SAMPLE_SIZE)
#define MICROFREAK_WAVE_MSG_SIZE (MICROFREAK_WAVE_BLK_SIZE * 8 / 7)	//32
#define MICROFREAK_PRESET_DATALEN (MICROFREAK_PRESET_PARTS * MICROFREAK_PRESET_PART_LEN)

struct microfreak_preset
{
  guint8 header[MICROFREAK_PRESET_HEADER_MSG_LEN];
  guint8 data[MICROFREAK_PRESET_DATALEN];
  guint parts;
};

// The size of this structure is 28 bytes and matches MICROFREAK_WAVE_BLK_SIZE.
struct microfreak_sample_header
{
  guint8 start[4];
  guint32 size;
  guint8 pad[2];
  gchar name[MICROFREAK_SAMPLE_NAME_LEN];
  guint8 id;
  guint8 end[4];
};

// The size of this structure is 28 bytes and matches MICROFREAK_WAVE_BLK_SIZE.
struct microfreak_wavetable_header
{
  guint8 id0;
  guint8 pad0[2];
  guint8 status0;
  guint8 data[4];
  guint8 id1;
  guint8 pad1;
  guint8 status1;
  guint8 status2;
  gchar name[MICROFREAK_WAVETABLE_NAME_LEN];
};

extern const struct connector CONNECTOR_MICROFREAK;

#endif
