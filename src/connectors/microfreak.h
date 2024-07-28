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

#include "connector.h"
#include "microfreak_sample.h"

#define MICROFREAK_NAME "microfreak"

#define MICROFREAK_PRESET_HEADER_MSG_LEN 0x23
#define MICROFREAK_PRESET_PARTS 146
#define MICROFREAK_PRESET_PART_LEN 0x20
#define MICROFREAK_SAMPLE_NAME_LEN 13	//Includes a NUL at the end
#define MICROFREAK_SAMPLE_BLOCK_SIZE 4096
#define MICROFREAK_SAMPLE_ITEM_MAX_TIME_S 24	// 375 blocks
#define MICROFREAK_SAMPLE_TOTAL_MAX_TIME_MS 209920	// Closest value to 210 s being multiple of MICROFREAK_SAMPLE_BLOCK_SIZE (3280 blocks)
#define MICROFREAK_SAMPLE_SIZE_PER_S (MICROFREAK_SAMPLERATE * MICROFREAK_SAMPLE_SIZE)	// at 32 kHz 16 bits
#define MICROFREAK_SAMPLE_MEM_SIZE ((uint32_t)(MICROFREAK_SAMPLE_TOTAL_MAX_TIME_MS * MICROFREAK_SAMPLE_SIZE_PER_S / 1000))
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

//Sample memory seems to start at 0x00281000. Addressing block size is MICROFREAK_SAMPLE_BLOCK_SIZE.
//When an upload is performed, the MicroFreak tries to find a gap in the address space.
//* If there is a big enough gap, it will be used; otherwise, the new sample is appended after the last used block.
//* If there is no space for a sample, no error is thrown and the process runs. Sometimes, an error is thrown and -ENOMEM is returned.
//This scheme may lead to fragmentation (the sum of the space in the gaps is enough for a sample but it does not fit at the end).
//Arturia MIDI Control Cernter provides this option when defragmentation is detected but Elektroid does not.
//This is totally independent of the slot used.
//Samples larger than MICROFREAK_SAMPLE_ITEM_MAX_TIME_S are handled by this connector by truncating the size when loading the sample. See microfreak_sample_load.
//If a sample does not fit, the MicroFreak returns an error which is handled by this connector with a -ENOMEM. See microfreak_sample_upload.
//Theoretical sample limit space is MICROFREAK_SAMPLE_MEM_SIZE (0x00cd0000) or 3280 blocks of MICROFREAK_SAMPLE_BLOCK_SIZE or roughly 210 s.

// The size of this structure is 28 bytes and matches MICROFREAK_WAVE_BLK_SIZE.
struct microfreak_sample_header
{
  guint32 address;		//Only used when reading the samples directory. Set to 0 when uploading.
  guint32 size;
  guint16 cksum;		//Values stored here do not seem to be important.
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

gint microfreak_sample_defragment (struct backend *backend);

#endif
