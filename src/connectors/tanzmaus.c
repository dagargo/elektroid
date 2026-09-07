/*
 *   tanzmaus.c
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
 *   Copyright (C) 2026 François Romain <contact@francoisromain.com>
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

#include "common.h"

/*
   The Tanzmaus stores 2 banks of 16 slots each.

   The code uses the terms bank and slot, both 0-based, where
   - a slot is a storage location on the machine
   - a sample (audio data) is uploaded into a slot

   The user-visible directory tree uses
   - the hardware labels `/SP1` and `/SP2`, alias to banks 0 and 1
   - slot numbers are received / displayed 1-based

   Example: the path /SP1/13 refers to bank 0, slot 12 in the code.
*/
#define MAX_SLOTS_PER_BANK 16

#define TANZMAUS_SAMPLE_RATE 44100

#define TANZMAUS_REST_TIME_US 22000

#define TANZMAUS_SAMPLES_PER_PAGE 264
#define TANZMAUS_SUBFRAMES_PER_PAGE 11
#define TANZMAUS_SAMPLES_PER_SUBFRAME 24

#define TANZMAUS_SLOT_HALF_SECOND 3
#define TANZMAUS_SLOT_ONE_SECOND 11

#define TANZMAUS_BANK_NAME "SP"

enum default_fs
{
  FS_TANZMAUS_SAMPLE
};

struct tanzmaus_iterator_data
{
  guint next;
  gint bank;
};

static guint
tanzmaus_slot_capacity (guint slot)
{
  if (slot <= TANZMAUS_SLOT_HALF_SECOND)
    {
      return 22000;
    }
  else if (slot <= TANZMAUS_SLOT_ONE_SECOND)
    {
      return 44000;
    }
  else
    {
      return 88000;
    }
}

static guint
tanzmaus_page_start_addr (guint bank, guint slot)
{
  if (slot <= TANZMAUS_SLOT_HALF_SECOND)
    {
      return (bank * 4 + slot) * 91;
    }
  else if (slot <= TANZMAUS_SLOT_ONE_SECOND)
    {
      return 728 + (bank * 8 + (slot - 4)) * 182;
    }
  else
    {
      return 3640 + (bank * 4 + (slot - 12)) * 364;
    }
}

static guint8
tanzmaus_crc7 (const guint8 *data, gsize len)
{
  guint8 crc = 0;
  for (gsize i = 0; i < len; i++)
    {
      guint8 byte = data[i];
      byte ^= crc << 1;
      if (byte & 0x80)
	{
	  byte ^= 9;
	}
      crc = byte ^ (crc & 0x78) ^ (crc << 4) ^ ((crc >> 3) & 15);
      crc &= 0x7f;
    }
  return crc;
}

static gint
tanzmaus_bank_from_path (const gchar *path)
{
  gsize len = strlen ("/" TANZMAUS_BANK_NAME "x");
  if (!strncmp (path, "/" TANZMAUS_BANK_NAME "1", len) &&
      (path[4] == '/' || path[4] == '\0'))
    {
      return 0;
    }
  else if (!strncmp (path, "/" TANZMAUS_BANK_NAME "2", len) &&
	   (path[4] == '/' || path[4] == '\0'))
    {
      return 1;
    }
  else
    {
      return -ENOTDIR;
    }
}

static gint
tanzmaus_get_bank_id_from_path (const gchar *path, guint *bank, guint *id)
{
  gint err;

  err = common_slot_get_id_from_path (path, id);
  if (err)
    {
      return err;
    }

  *bank = tanzmaus_bank_from_path (path);
  if (*bank < 0)
    {
      return *bank;
    }

  (*id)--;

  return 0;
}

static GByteArray *
tanzmaus_sysex_slot_select (guint slot)
{
  GByteArray *msg = g_byte_array_sized_new (9);
  guint8 buf[] = { 0xf0, 0x00, 0x21, 0x0b, 0x04, 0x00, 0x06, slot & 0x0f,
    0xf7
  };
  g_byte_array_append (msg, buf, sizeof (buf));
  return msg;
}

static GByteArray *
tanzmaus_sysex_data_page (guint addr, guint sub_idx, const guint8 *samples)
{
  GByteArray *msg = g_byte_array_sized_new (60);

  guint8 header[] = { 0xf0, 0x00, 0x21, 0x0b, 0x04, 0x00, 0x05 };
  g_byte_array_append (msg, header, sizeof (header));

  guint8 addr_lo = addr & 0x7f;
  guint8 addr_hi = (addr >> 7) & 0x7f;
  g_byte_array_append (msg, &addr_lo, 1);
  g_byte_array_append (msg, &addr_hi, 1);

  guint8 sub = sub_idx & 0x7f;
  g_byte_array_append (msg, &sub, 1);

  g_byte_array_append (msg, samples, 48);

  guint8 crc = tanzmaus_crc7 (msg->data + 1, 57);
  g_byte_array_append (msg, &crc, 1);

  guint8 eox = 0xf7;
  g_byte_array_append (msg, &eox, 1);

  return msg;
}

static GByteArray *
tanzmaus_sysex_end_upload (void)
{
  GByteArray *msg = g_byte_array_sized_new (8);
  guint8 buf[] = { 0xf0, 0x00, 0x21, 0x0b, 0x04, 0x00, 0x07, 0xf7 };
  g_byte_array_append (msg, buf, sizeof (buf));
  return msg;
}

static gint
tanzmaus_next_dentry_root (struct item_iterator *iter)
{
  struct tanzmaus_iterator_data *data = iter->data;

  if (data->next <= 2)
    {
      iter->item.id = 0x1000 + data->next;
      iter->item.slot[0] = 0;
      item_set_name (&iter->item, "%s%d", TANZMAUS_BANK_NAME, data->next);
      iter->item.type = ITEM_TYPE_DIR;
      iter->item.size = -1;

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
tanzmaus_next_dentry_bank (struct item_iterator *iter)
{
  struct tanzmaus_iterator_data *data = iter->data;

  if (data->next <= MAX_SLOTS_PER_BANK)
    {
      iter->item.id = data->next;
      snprintf (iter->item.slot, ITEM_SLOT_MAX, "%d", data->next);
      guint len = tanzmaus_slot_capacity (data->next - 1);
      gdouble s = len / (double) TANZMAUS_SAMPLE_RATE;
      item_set_name (&iter->item, "%d (%.1f s)", data->next, s);
      iter->item.type = ITEM_TYPE_FILE;
      iter->item.size = len * 2;

      data->next++;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
}

static gint
tanzmaus_read_dir (struct backend *backend, struct item_iterator *iter,
		   const gchar *dir, const gchar **extensions)
{
  if (!strcmp (dir, "/"))
    {
      struct tanzmaus_iterator_data *data =
	g_malloc (sizeof (struct tanzmaus_iterator_data));
      data->next = 1;
      data->bank = -1;
      item_iterator_init (iter, dir, data, tanzmaus_next_dentry_root, g_free);
      return 0;
    }
  else
    {
      gint bank = tanzmaus_bank_from_path (dir);
      if (bank < 0)
	{
	  return -ENOTDIR;
	}

      struct tanzmaus_iterator_data *data =
	g_malloc (sizeof (struct tanzmaus_iterator_data));
      data->next = 1;
      data->bank = bank;
      item_iterator_init (iter, dir, data, tanzmaus_next_dentry_bank, g_free);
      return 0;
    }
}

static gint
tanzmaus_sample_upload (struct backend *backend, const gchar *path,
			struct idata *sample, struct task_control *control)
{
  guint bank, id;
  gint err;
  GByteArray *input = sample->content;
  GByteArray *msg;

  err = tanzmaus_get_bank_id_from_path (path, &bank, &id);
  if (err)
    {
      return err;
    }

  guint capacity = tanzmaus_slot_capacity (id);
  guint num_samples = input->len / 2;
  if (num_samples > capacity)
    {
      num_samples = capacity;
    }

  guint padded_samples =
    ((num_samples + TANZMAUS_SAMPLES_PER_PAGE -
      1) / TANZMAUS_SAMPLES_PER_PAGE) * TANZMAUS_SAMPLES_PER_PAGE;
  guint num_pages = padded_samples / TANZMAUS_SAMPLES_PER_PAGE;
  guint page_start = tanzmaus_page_start_addr (bank, id);
  guint total_subframes = num_pages * TANZMAUS_SUBFRAMES_PER_PAGE;
  guint subframe_count = 0;

  g_mutex_lock (&backend->mutex);
  backend_rx_drain (backend);
  g_mutex_unlock (&backend->mutex);

  task_control_reset (control, 1);

  msg = tanzmaus_sysex_slot_select (id);
  err = backend_tx (backend, msg);
  if (err)
    {
      return err;
    }
  g_usleep (TANZMAUS_REST_TIME_US);

  gint16 *samples = (gint16 *) input->data;

  for (guint page = 0; page < num_pages; page++)
    {
      guint page_addr = page_start + page;

      for (guint sub = 0; sub < TANZMAUS_SUBFRAMES_PER_PAGE; sub++)
	{
	  guint8 page_data[TANZMAUS_SAMPLES_PER_SUBFRAME * 2];
	  guint sample_offset =
	    page * TANZMAUS_SAMPLES_PER_PAGE +
	    sub * TANZMAUS_SAMPLES_PER_SUBFRAME;

	  for (guint s = 0; s < TANZMAUS_SAMPLES_PER_SUBFRAME; s++)
	    {
	      guint idx = sample_offset + s;
	      gint16 raw;

	      if (idx < num_samples)
		{
		  raw = samples[idx];
		}
	      else
		{
		  raw = 0;
		}

	      guint16 unsigned_val = (guint16) (raw + 32768);
	      guint16 twelve_bit = unsigned_val >> 4;

	      page_data[s * 2] = twelve_bit & 0x7f;
	      page_data[s * 2 + 1] = (twelve_bit >> 7) & 0x7f;
	    }

	  msg = tanzmaus_sysex_data_page (page_addr, sub, page_data);
	  err = backend_tx (backend, msg);
	  if (err)
	    {
	      return err;
	    }

	  g_usleep (TANZMAUS_REST_TIME_US);

	  subframe_count++;
	  task_control_set_progress (control,
				     subframe_count /
				     (gdouble) total_subframes);

	  if (!controllable_is_active (&control->controllable))
	    {
	      return -ECANCELED;
	    }
	}
    }

  msg = tanzmaus_sysex_end_upload ();
  err = backend_tx (backend, msg);

  task_control_set_progress (control, 1.0);
  return err;
}

static gint
tanzmaus_sample_load (struct backend *backend, const gchar *path,
		      struct idata *sample, struct task_control *control)
{
  return common_sample_load (path, sample, control, 1, TANZMAUS_SAMPLE_RATE,
			     SF_FORMAT_PCM_16, FALSE);
}

static const struct fs_operations FS_TANZMAUS_SAMPLE_OPERATIONS = {
  .id = FS_TANZMAUS_SAMPLE,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SHOW_SLOT_COLUMN |
    FS_OPTION_SHOW_SIZE_COLUMN,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = FS_ICON_WAVE,
  .file_icon = FS_ICON_WAVE,
  .readdir = tanzmaus_read_dir,
  .upload = tanzmaus_sample_upload,
  .load = tanzmaus_sample_load,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path
};

static gint
tanzmaus_handshake (struct backend *backend)
{
  gslist_fill (&backend->fs_ops, &FS_TANZMAUS_SAMPLE_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "%s", "MFB Tanzmaus");
  return 0;
}

const struct connector CONNECTOR_TANZMAUS = {
  .name = "tanzmaus",
  .handshake = tanzmaus_handshake,
  .type = CONNECTOR_TYPE_MIDI_NO_HANDSHAKE
};
