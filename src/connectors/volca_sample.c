/*
 *   volca_sample.c
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

#include "audio.h"
#include "sample.h"
#include "common.h"
#include "volca_sample_sdk/korg_syro_volcasample.h"

#define VOLCA_SAMPLE_MAX_SAMPLES 100

#define VOLCA_SAMPLE_RATE 31250
#define VOLCA_SAMPLE_SYRO_RATE 44100

#define VOLCA_SAMPLE_CHANNELS 1
#define VOLCA_SAMPLE_SYRO_CHANNELS 2

#define VOLCA_SAMPLE_DEVICE_NAME "KORG Volca Sample"

#define VOLCA_SAMPLE_SLEEP_US 200000

#define VOLCA_SAMPLE_UPLOAD_STAGES 4

enum volca_sample_fs
{
  FS_VOLCA_SAMPLE_16B,
  FS_VOLCA_SAMPLE_8B,
};

static gint
volca_sample_read_dir (struct backend *backend, struct item_iterator *iter,
		       const gchar *dir, const gchar **extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 0;
  data->last = VOLCA_SAMPLE_MAX_SAMPLES - 1;

  item_iterator_init (iter, dir, data, common_simple_next_dentry, g_free);

  return 0;
}

static gint
volca_sample_send (struct idata *syro, struct job_control *control)
{
  gint err;
  struct idata sample;
  struct sample_info native_si;

  //Native samplerate and sample type conversion

  sample_info_init_load (&native_si, VOLCA_SAMPLE_SYRO_CHANNELS, audio.rate,
			 sample_get_internal_format ());

  err = sample_reload (syro, &sample, control, &native_si,
		       job_control_set_sample_progress);
  if (err)
    {
      return err;
    }

  if (control)
    {
      control->part++;
    }

  //Playback

  audio_set_play_and_wait (&sample, control);

  usleep (VOLCA_SAMPLE_SLEEP_US);

  if (control)
    {
      control->part++;
    }

  idata_free (syro);

  return 0;
}

gint
volca_sample_get_syro_op (SyroData *data, struct idata *syro_op,
			  struct job_control *control)
{
  guint32 i, j, update_frames, frames;
  gint16 left, right;
  SyroHandle handle;
  SyroStatus status;
  GByteArray *content;
  struct sample_info *syro_si;

  status = SyroVolcaSample_Start (&handle, data, 1, 0, &frames);
  if (status != Status_Success)
    {
      return -EIO;
    }

  debug_print (1, "Reported %d SYRO frames", frames);

  syro_si = g_malloc (sizeof (struct sample_info));
  syro_si->frames = frames;
  syro_si->loop_start = frames - 1;
  syro_si->loop_end = syro_si->loop_start;
  syro_si->loop_type = 0;
  syro_si->rate = VOLCA_SAMPLE_SYRO_RATE;
  syro_si->format = SF_FORMAT_PCM_16;
  syro_si->channels = VOLCA_SAMPLE_SYRO_CHANNELS;
  syro_si->midi_note = 0;
  syro_si->midi_fraction = 0;

  content = g_byte_array_sized_new (frames * VOLCA_SAMPLE_SYRO_CHANNELS *
				    sizeof (gint16));

  idata_init (syro_op, content, NULL, syro_si);

  i = 0;
  j = 0;
  update_frames = frames / (100 / VOLCA_SAMPLE_UPLOAD_STAGES);
  while (i < frames)
    {
      // The returning value is ignored in korg_syro_volcasample_example.c.
      // This functions fails when using DataType_Sample_Linear with samples
      // longer than 4000 frames. The actual limit is lower.
      status = SyroVolcaSample_GetSample (handle, &left, &right);
      if (status != Status_Success)
	{
	  error_print ("Error at frame %d: %d", i, status);
	  SyroVolcaSample_End (handle);
	  idata_free (syro_op);
	  return -EIO;
	}

      g_byte_array_append (content, (guint8 *) & left, sizeof (gint16));
      g_byte_array_append (content, (guint8 *) & right, sizeof (gint16));

      i++;
      j++;

      if (control && j == update_frames)
	{
	  job_control_set_progress (control, i / (gdouble) frames);
	  update_frames = 0;
	}
    }

  debug_print (1, "Read %d SYRO frames", syro_si->frames);

  status = SyroVolcaSample_End (handle);
  if (status != Status_Success)
    {
      idata_free (syro_op);
      return -EIO;
    }

  if (control)
    {
      control->part++;
    }

  return 0;
}

gint
volca_sample_get_upload (guint id, struct idata *input, struct idata *syro_op,
			 guint32 quality, struct job_control *control)
{
  SyroData data;

  // DataType_Sample_Compress uses quality between 8 and 16.
  // DataType_Sample_Linear uses quality 0 but DataType_Sample_Linear only
  // works for really short samples and, based on the official documentation
  // in https://korginc.github.io/volcasample/documentation.html, it is not
  // even a valid value.
  data.DataType = DataType_Sample_Compress;
  data.pData = input->content->data;
  data.Number = id;
  data.Size = input->content->len;
  data.Quality = quality;
  data.Fs = VOLCA_SAMPLE_RATE;
  data.SampleEndian = LittleEndian;

  return volca_sample_get_syro_op (&data, syro_op, control);
}

static gint
volca_sample_upload_quality (const gchar *path, struct idata *sample,
			     guint32 quality, struct job_control *control)
{
  guint id;
  gint err;
  struct idata syro_op;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (id >= VOLCA_SAMPLE_MAX_SAMPLES)
    {
      return -EINVAL;
    }

  //Syro conversion

  err = volca_sample_get_upload (id, sample, &syro_op, quality, control);
  if (err)
    {
      return err;
    }

  return volca_sample_send (&syro_op, control);
}

static gint
volca_sample_upload_16b (struct backend *backend, const gchar *path,
			 struct idata *sample, struct job_control *control)
{
  return volca_sample_upload_quality (path, sample, 16, control);
}

static gint
volca_sample_upload_8b (struct backend *backend, const gchar *path,
			struct idata *sample, struct job_control *control)
{
  return volca_sample_upload_quality (path, sample, 8, control);
}

static gint
volca_sample_load (const gchar *path, struct idata *sample,
		   struct job_control *control)
{
  gint err;

  job_control_reset (control, VOLCA_SAMPLE_UPLOAD_STAGES);

  //31250 Hz 16 bit conversion

  err = common_sample_load (path, sample, control, VOLCA_SAMPLE_RATE,
			    VOLCA_SAMPLE_CHANNELS, SF_FORMAT_PCM_16);
  if (err)
    {
      return err;
    }

  control->part++;

  return 0;
}

gint
volca_sample_get_delete (guint id, struct idata *syro_op)
{
  SyroData data;

  data.DataType = DataType_Sample_Erase;
  data.pData = NULL;
  data.Number = id;
  data.SampleEndian = LittleEndian;

  return volca_sample_get_syro_op (&data, syro_op, NULL);
}

static gint
volca_sample_delete (struct backend *backend, const gchar *path)
{
  guint id;
  gint err;
  struct idata syro_op;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (id >= VOLCA_SAMPLE_MAX_SAMPLES)
    {
      return -EINVAL;
    }

  err = volca_sample_get_delete (id, &syro_op);
  if (err)
    {
      return err;
    }

  return volca_sample_send (&syro_op, NULL);
}

static const struct fs_operations FS_VOLCA_SAMPLE_16B_OPERATIONS = {
  .id = FS_VOLCA_SAMPLE_16B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_AUDIO_LINK,
  .name = "sample-16b",
  .gui_name = "Samples 16 bits",
  .gui_icon = FS_ICON_WAVE,
  .readdir = volca_sample_read_dir,
  .delete = volca_sample_delete,
  .print_item = common_print_item,
  .upload = volca_sample_upload_16b,
  .load = volca_sample_load,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path
};

static const struct fs_operations FS_VOLCA_SAMPLE_8B_OPERATIONS = {
  .id = FS_VOLCA_SAMPLE_8B,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_AUDIO_LINK,
  .name = "sample-8b",
  .gui_name = "Samples 8 bits",
  .gui_icon = FS_ICON_WAVE,
  .readdir = volca_sample_read_dir,
  .delete = volca_sample_delete,
  .print_item = common_print_item,
  .upload = volca_sample_upload_8b,
  .load = volca_sample_load,
  .get_exts = sample_get_sample_extensions,
  .get_upload_path = common_slot_get_upload_path
};


static gint
volca_sample_handshake (struct backend *backend)
{
  gslist_fill (&backend->fs_ops, &FS_VOLCA_SAMPLE_16B_OPERATIONS,
	       &FS_VOLCA_SAMPLE_8B_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, VOLCA_SAMPLE_DEVICE_NAME);
  return 0;
}

const struct connector CONNECTOR_VOLCA_SAMPLE = {
  .name = "volca-sample",
  .handshake = volca_sample_handshake,
  .options = CONNECTOR_OPTION_NO_MIDI,
  .device_name = VOLCA_SAMPLE_DEVICE_NAME
};
