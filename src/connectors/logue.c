/*
 *   logue.c
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
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

#include <asm-generic/errno-base.h>
#include <json-glib/json-glib.h>
#include <zip.h>
#include <zlib.h>
#include "common.h"
#include "logue.h"

#define UNIT_MANIFEST_FILE "manifest.json"
#define UNIT_PAYLOAD_FILE "payload.bin"

#define LOGUE_REST_TIME_US 10000

#define LOGUE_NAME_PROLOGUE "prologue"
#define LOGUE_NAME_MINILOGUE_XD "minilogue xd"
#define LOGUE_NAME_NTS1 "NTS-1"

#define LOGUE_PLATFORM_NAME_PROLOGUE "prologue"
#define LOGUE_PLATFORM_NAME_MINILOGUE_XD "minilogue-xd"
#define LOGUE_PLATFORM_NAME_NTS1 "nutekt-digital"

#define LOGUE_SLOT_STATUS_PARAMS_MAX 24
#define LOGUE_SLOT_STATUS_NAME_LEN 14
#define LOGUE_SLOT_STATUS_PARAM_NAME_LEN 13

#define LOGUE_MANIFEST_HEADER "header"
#define LOGUE_MANIFEST_PLATFORM "platform"
#define LOGUE_MANIFEST_MODULE "module"
#define LOGUE_MANIFEST_API "api"
#define LOGUE_MANIFEST_DEV_ID "dev_id"
#define LOGUE_MANIFEST_PROGRAM_ID "prg_id"
#define LOGUE_MANIFEST_VERSION "version"
#define LOGUE_MANIFEST_NAME "name"
#define LOGUE_MANIFEST_NUM_PARAM "num_param"
#define LOGUE_MANIFEST_PARAMS "params"

struct logue_version
{
  guint8 reserved;
  guint8 major;
  guint8 minor;
  guint8 patch;
};

struct logue_data
{
  guint8 device;
  enum logue_platform platform;
};

struct logue_iter_data
{
  guint next;
  guint slots;
  enum logue_module module;
  struct backend *backend;
};

enum logue_slot_parameter_type
{
  LOGUE_SLOT_PARAMETER_TYPE_PERCENT = 0,
  LOGUE_SLOT_PARAMETER_TYPE_NONE = 2
};

struct logue_slot_parameter
{
  guint8 min;
  guint8 max;
  enum logue_slot_parameter_type type;
  gchar name[LOGUE_SLOT_STATUS_PARAM_NAME_LEN + 1];
};

// NOTE: PLATFORM ID and MODULE ID seem to be swapped in the official documentation.
// NOTE: The reserved bytes in API and program version seem to not be in the position explained in the official documentation.
struct logue_slot_status
{
  guint8 module_id;
  guint8 platform_id;
  struct logue_version api_version;
  guint32 developer_id;
  guint32 program_id;
  struct logue_version program_version;
  gchar name[LOGUE_SLOT_STATUS_NAME_LEN];
};

struct logue_manifest
{
  struct logue_slot_status status;
  guint32 param_num;
  struct logue_slot_parameter parameters[LOGUE_SLOT_STATUS_PARAMS_MAX];
};

#define LOGUE_GET_MSG_OP(msg) (msg->data[6])
#define LOGUE_CHECK_STATUS_MSG_COMPLETED(msg) (msg->len == 8 && LOGUE_GET_MSG_OP(msg) == 0x23)

static const guint8 MSG_HEADER[] = { 0xf0, 0x42, 0x30, 0, 1 };

static const guint8 LOGUE_SEARCH_DEVICE[] =
  { 0xf0, 0x42, 0x50, 0, 0x55, 0xf7 };

static const gchar *LOGUE_NTS1_EXTS[] = { "syx", "ntkdigunit", NULL };
static const gchar *LOGUE_PROLOGUE_EXTS[] = { "syx", "prlgunit", NULL };
static const gchar *LOGUE_MINILOGUEXD_EXTS[] = { "syx", "mnlgxdunit", NULL };

static gboolean
logue_validate_device (GByteArray *msg, guint8 device_id)
{
  return (msg->data[6] == device_id && msg->data[7] == 1 &&
	  msg->data[8] == 0 && msg->data[9] == 0);
}

static GByteArray *
logue_get_msg (guint8 device, guint8 op, const guint8 *data, guint len)
{
  GByteArray *msg;

  msg = g_byte_array_sized_new (sizeof (MSG_HEADER) + 4 + len);
  g_byte_array_append (msg, MSG_HEADER, sizeof (MSG_HEADER));
  g_byte_array_append (msg, &device, 1);
  g_byte_array_append (msg, &op, 1);
  if (data && len > 0)
    {
      g_byte_array_append (msg, data, len);
    }
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  return msg;
}

static GByteArray *
logue_get_msg_op_type (guint8 device, guint8 op, guint8 module)
{
  return logue_get_msg (device, op, &module, 1);
}

static GByteArray *
logue_get_msg_op_type_id (guint8 device, guint8 op, guint8 module,
			  guint8 slot)
{
  guint8 data[2];
  data[0] = module;
  data[1] = slot;
  return logue_get_msg (device, op, data, 2);
}

static gint
logue_next_dentry (struct item_iterator *iter)
{
  GByteArray *tx_msg, *rx_msg;
  struct logue_iter_data *iter_data = iter->data;
  struct logue_data *logue_data = iter_data->backend->data;

  if (iter_data->next >= iter_data->slots)
    {
      return -ENOENT;
    }

  tx_msg = logue_get_msg_op_type_id (logue_data->device, 0x19,
				     iter_data->module, iter_data->next);
  rx_msg = backend_tx_and_rx_sysex (iter_data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (rx_msg->len == 53 && LOGUE_GET_MSG_OP (rx_msg) == 0x49)
    {
      gsize size;
      GByteArray *content;

      // NOTE: There is an unknown byte that is not in the official documentation.
      // 8 bit payload length is also wrong as it is 36.
      size = common_midi_msg_to_8bit_msg_size (42);
      content = g_byte_array_sized_new (size);
      content->len = size;
      common_midi_msg_to_8bit_msg (&rx_msg->data[10], content->data, 42);

      if (debug_level > 1)
	{
	  gchar *text = debug_get_hex_data (debug_level, content->data,
					    content->len);
	  debug_print (1, "Message received (%u): %s", content->len, text);
	  g_free (text);
	}

      // name is up to LOGUE_SLOT_STATUS_NAME_LEN characters including the null at the end
      item_set_name (&iter->item, "%s", (gchar *) & content->data[18]);
      free_msg (content);
    }
  else
    {
      item_set_name (&iter->item, "");
    }

  iter->item.id = iter_data->next;
  iter->item.type = ITEM_TYPE_FILE;
  iter->item.size = -1;

  iter_data->next++;

  free_msg (rx_msg);

  return 0;
}

static gint
logue_read_dir (struct backend *backend,
		struct item_iterator *iter, const gchar *dir,
		const gchar **extensions, enum logue_module module)
{
  gint err;
  GByteArray *tx_msg, *rx_msg;
  struct logue_iter_data *data;
  struct logue_data *logue_data = backend->data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  tx_msg = logue_get_msg_op_type (logue_data->device, 0x18, module);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -ENODEV;
    }
  if (rx_msg->len == 24 && LOGUE_GET_MSG_OP (rx_msg) == 0x48)
    {
      guint size;
      guint32 storage_size, load_size, slot_count;
      GByteArray *content;

      // NOTE: There are 2 unknown byte that is not in the official documentation.
      // 8 bit payload length is also wrong as it is 12.
      size = common_midi_msg_to_8bit_msg_size (14);
      content = g_byte_array_sized_new (size);
      content->len = size;
      common_midi_msg_to_8bit_msg (&rx_msg->data[9], content->data, 14);

      if (debug_level > 1)
	{
	  gchar *text = debug_get_hex_data (debug_level, content->data,
					    content->len);
	  debug_print (1, "Message received (%u): %s", content->len, text);
	  g_free (text);
	}

      storage_size = GUINT32_FROM_LE (*((guint32 *) (&content->data[0])));
      load_size = GUINT32_FROM_LE (*((guint32 *) (&content->data[4])));
      slot_count = GUINT32_FROM_LE (*((guint32 *) (&content->data[8])));

      debug_print (2, "Storage size: %d B", storage_size);
      debug_print (2, "Load size: %d B", load_size);
      debug_print (2, "Slot count: %d", slot_count);

      data = g_malloc (sizeof (struct logue_iter_data));
      data->next = 0;
      data->slots = slot_count;
      data->backend = backend;
      data->module = module;

      free_msg (content);

      item_iterator_init (iter, dir, data, logue_next_dentry, g_free);

      err = 0;
    }
  else
    {
      err = -EIO;
    }

  free_msg (rx_msg);

  return err;
}

static gint
logue_osc_read_dir (struct backend *backend,
		    struct item_iterator *iter, const gchar *dir,
		    const gchar **extensions)
{
  return logue_read_dir (backend, iter, dir, extensions, FS_LOGUE_MODULE_OSC);
}

static gint
logue_modfx_read_dir (struct backend *backend,
		      struct item_iterator *iter, const gchar *dir,
		      const gchar **extensions)
{
  return logue_read_dir (backend, iter, dir, extensions,
			 FS_LOGUE_MODULE_MODFX);
}

static gint
logue_delfx_read_dir (struct backend *backend,
		      struct item_iterator *iter, const gchar *dir,
		      const gchar **extensions)
{
  return logue_read_dir (backend, iter, dir, extensions,
			 FS_LOGUE_MODULE_DELFX);
}

static gint
logue_revfx_read_dir (struct backend *backend,
		      struct item_iterator *iter, const gchar *dir,
		      const gchar **extensions)
{
  return logue_read_dir (backend, iter, dir, extensions,
			 FS_LOGUE_MODULE_REVFX);
}

static const gchar **
logue_get_extensions (struct backend *backend,
		      const struct fs_operations *ops)
{
  struct logue_data *data = backend->data;
  if (data->device == LOGUE_DEVICE_PROLOGUE)
    {
      return LOGUE_PROLOGUE_EXTS;
    }
  else if (data->device == LOGUE_DEVICE_MINILOGUE_XD)
    {
      return LOGUE_MINILOGUEXD_EXTS;
    }
  else if (data->device == LOGUE_DEVICE_NTS1)
    {
      return LOGUE_NTS1_EXTS;
    }
  else
    {
      return NULL;
    }
}

static gchar *
logue_get_id_as_slot (struct item *item, struct backend *backend)
{
  return common_get_id_as_slot_padded (item, backend, 2);
}

static gint
logue_clear (struct backend *backend, const gchar *path,
	     enum logue_module module)
{
  gint err;
  guint id;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data = backend->data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  tx_msg = logue_get_msg_op_type_id (logue_data->device, 0x1b, module, id);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }
  if (!LOGUE_CHECK_STATUS_MSG_COMPLETED (rx_msg))
    {
      err = -EIO;
    }

  free_msg (rx_msg);

  return err;
}

static gint
logue_osc_clear (struct backend *backend, const gchar *path)
{
  return logue_clear (backend, path, FS_LOGUE_MODULE_OSC);
}

static gint
logue_modfx_clear (struct backend *backend, const gchar *path)
{
  return logue_clear (backend, path, FS_LOGUE_MODULE_MODFX);
}

static gint
logue_delfx_clear (struct backend *backend, const gchar *path)
{
  return logue_clear (backend, path, FS_LOGUE_MODULE_DELFX);
}

static gint
logue_revfx_clear (struct backend *backend, const gchar *path)
{
  return logue_clear (backend, path, FS_LOGUE_MODULE_REVFX);
}

gint
logue_get_manifest (GByteArray *manifest_content,
		    struct logue_manifest *manifest)
{
  gint err;
  gchar *aux;
  GError *error;
  JsonParser *parser;
  JsonReader *reader;
  const gchar *string;

  err = 0;

  parser = json_parser_new ();
  error = NULL;
  if (!json_parser_load_from_data (parser, (gchar *) manifest_content->data,
				   manifest_content->len, &error))
    {
      error_print ("Unable to parse stream: %s", error->message);
      g_clear_error (&error);
      err = -EINVAL;
      goto cleanup_parser;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser");
      goto cleanup_parser;
    }

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_HEADER))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_HEADER);
      err = -EINVAL;
      goto cleanup_reader;
    }

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_PLATFORM))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_PLATFORM);
      err = -EINVAL;
      goto cleanup_reader;
    }
  string = json_reader_get_string_value (reader);
  if (!strcmp (string, LOGUE_PLATFORM_NAME_PROLOGUE))
    {
      manifest->status.platform_id = LOGUE_PLATFORM_PROLOGUE;
    }
  else if (!strcmp (string, LOGUE_PLATFORM_NAME_MINILOGUE_XD))
    {
      manifest->status.platform_id = LOGUE_PLATFORM_MINILOGUE_XD;
    }
  else if (!strcmp (string, LOGUE_PLATFORM_NAME_NTS1))
    {
      manifest->status.platform_id = LOGUE_PLATFORM_NTS1;
    }
  else
    {
      err = -EINVAL;
      goto cleanup_reader;
    }
  debug_print (2, "Platform: %s (%d)", string, manifest->status.platform_id);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_MODULE))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_MODULE);
      err = -EINVAL;
      goto cleanup_reader;
    }
  string = json_reader_get_string_value (reader);
  if (!strcmp (string, "modfx"))
    {
      manifest->status.module_id = FS_LOGUE_MODULE_MODFX;
    }
  else if (!strcmp (string, "delfx"))
    {
      manifest->status.module_id = FS_LOGUE_MODULE_DELFX;
    }
  else if (!strcmp (string, "revfx"))
    {
      manifest->status.module_id = FS_LOGUE_MODULE_REVFX;
    }
  else if (!strcmp (string, "osc"))
    {
      manifest->status.module_id = FS_LOGUE_MODULE_OSC;
    }
  else
    {
      err = -EINVAL;
      goto cleanup_reader;
    }
  debug_print (2, "Module: %s (%d)", string, manifest->status.module_id);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_API))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_API);
      err = -EINVAL;
      goto cleanup_reader;
    }
  string = json_reader_get_string_value (reader);
  manifest->status.api_version.major = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.api_version.minor = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.api_version.patch = strtol (string, &aux, 10);
  debug_print (2, "API: %d.%d-%d", manifest->status.api_version.major,
	       manifest->status.api_version.minor,
	       manifest->status.api_version.patch);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_DEV_ID))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_DEV_ID);
      err = -EINVAL;
      goto cleanup_reader;
    }
  manifest->status.developer_id = json_reader_get_int_value (reader);
  debug_print (2, "Developer ID: %d", manifest->status.developer_id);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_PROGRAM_ID))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_PROGRAM_ID);
      err = -EINVAL;
      goto cleanup_reader;
    }
  manifest->status.program_id = json_reader_get_int_value (reader);
  debug_print (2, "Program ID: %d", manifest->status.program_id);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_VERSION))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_VERSION);
      err = -EINVAL;
      goto cleanup_reader;
    }
  string = json_reader_get_string_value (reader);
  manifest->status.program_version.major = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.program_version.minor = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.program_version.patch = strtol (string, &aux, 10);
  debug_print (2, "Version: %d.%d-%d", manifest->status.program_version.major,
	       manifest->status.program_version.minor,
	       manifest->status.program_version.patch);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_NAME))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_NAME);
      err = -EINVAL;
      goto cleanup_reader;
    }
  string = json_reader_get_string_value (reader);
  memset (manifest->status.name, 0, LOGUE_SLOT_STATUS_NAME_LEN);
  snprintf (manifest->status.name, LOGUE_SLOT_STATUS_NAME_LEN, "%s", string);
  debug_print (2, "Name: %s", manifest->status.name);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, LOGUE_MANIFEST_NUM_PARAM))
    {
      error_print ("No '%s' found", LOGUE_MANIFEST_NUM_PARAM);
      err = -EINVAL;
      goto cleanup_reader;
    }
  manifest->param_num = json_reader_get_int_value (reader);
  debug_print (2, "Number of parameters: %d", manifest->param_num);
  if (manifest->status.module_id == FS_LOGUE_MODULE_OSC)
    {
      if (manifest->param_num > 6)
	{
	  error_print ("Illegal number of parameters: %d",
		       manifest->param_num);
	  err = -EINVAL;
	  goto cleanup_reader;
	}
    }
  else
    {
      if (manifest->param_num > 24)
	{
	  error_print ("Illegal number of parameters: %d",
		       manifest->param_num);
	  err = -EINVAL;
	  goto cleanup_reader;
	}
    }
  json_reader_end_element (reader);

  if (manifest->param_num > 0)
    {
      if (!json_reader_read_member (reader, LOGUE_MANIFEST_PARAMS))
	{
	  error_print ("No '%s' found", LOGUE_MANIFEST_PARAMS);
	  err = -EINVAL;
	  goto cleanup_reader;
	}
      if (!json_reader_is_array (reader))
	{
	  error_print ("Member '%s' is not an array", LOGUE_MANIFEST_PARAMS);
	  err = -EINVAL;
	  goto cleanup_reader;
	}
      if (json_reader_count_elements (reader) != manifest->param_num)
	{
	  error_print ("Inconsistent number of parameters");
	  err = -EINVAL;
	  goto cleanup_reader;
	}

      for (guint i = 0; i < manifest->param_num; i++)
	{
	  if (!json_reader_read_element (reader, i))
	    {
	      error_print ("Parameter %d not found", i);
	      err = -EINVAL;
	      goto cleanup_reader;
	    }

	  if (!json_reader_is_array (reader))
	    {
	      error_print ("Illegal parameter format");
	      err = -EINVAL;
	      goto cleanup_reader;
	    }

	  if (json_reader_count_elements (reader) != 4)
	    {
	      error_print ("Illegal parameters format");
	      err = -EINVAL;
	      goto cleanup_reader;
	    }

	  json_reader_read_element (reader, 0);
	  string = json_reader_get_string_value (reader);
	  memset (manifest->parameters[i].name, 0,
		  LOGUE_SLOT_STATUS_PARAM_NAME_LEN + 1);
	  snprintf (manifest->parameters[i].name,
		    LOGUE_SLOT_STATUS_PARAM_NAME_LEN + 1, "%s", string);
	  json_reader_end_element (reader);

	  json_reader_read_element (reader, 1);
	  manifest->parameters[i].min = json_reader_get_int_value (reader);
	  json_reader_end_element (reader);

	  json_reader_read_element (reader, 2);
	  manifest->parameters[i].max = json_reader_get_int_value (reader);
	  json_reader_end_element (reader);

	  json_reader_read_element (reader, 3);
	  string = json_reader_get_string_value (reader);
	  if (!strcmp (string, "%"))
	    {
	      manifest->parameters[i].type =
		LOGUE_SLOT_PARAMETER_TYPE_PERCENT;
	    }
	  else if (!strcmp (string, ""))
	    {
	      manifest->parameters[i].type = LOGUE_SLOT_PARAMETER_TYPE_NONE;
	    }
	  else
	    {
	      error_print ("Illegal parameter type");
	      err = -EINVAL;
	      goto cleanup_reader;
	    }
	  json_reader_end_element (reader);

	  json_reader_end_element (reader);

	}

      json_reader_end_element (reader);
    }

  json_reader_end_element (reader);

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);

  return err;
}

// Complete message: f0 42 30 00 01 57 MM SS PP .. PP 00 f7
// SysEx header
// 1 byte for the module
// 1 byte for the slot
// MIDI encoded payload
// null byte
// SysEx end

// Payload (part 1) 400 B
// size  (u32)
// crc32 (u32)
// 04 03 00 01 01 00 00 00 00 00 00 00 00 00 00 01 -> module slot 0 ...0 1
// 00 00 6d 61 73 73 00 00 00 00 00 00 00 00 00 00 -> 0 0 name(14)
// 02 00 00 00 00 06 02 56 6f 69 63 65 73 00 00 00 -> params param1 (0 0 0 min max type name(9))
// 00 00 00 00 00 64 00 42 65 61 74 69 6e 67 00 00 -> 0      param2 (0 0 0 min max type name(9))
// Payload (part 2) remaining bytes

gint
logue_unit_load (const char *path, struct idata *sysex,
		 struct task_control *control)
{
  gint err;
  gchar *name;
  zip_t *unit;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_int64_t entries;
  guint32 v, len, crc;
  gchar *unit_name = NULL;
  gchar entry_name[PATH_MAX];
  struct logue_manifest logue_manifest;
  zip_file_t *unit_payload = NULL;
  zip_file_t *unit_manifest = NULL;
  GByteArray *msg, *manifest = NULL, *payload =
    NULL, *msg_payload_midi, *msg_payload_8bit;

  unit = zip_open (path, ZIP_RDONLY, &err);
  if (!unit)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while opening zip file: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  entries = zip_get_num_entries (unit, 0);
  if (entries != 3)
    {
      err = -EIO;
      goto end;
    }
  for (guint i = 0; i < 3; i++)
    {
      if (zip_stat_index (unit, i, ZIP_FL_ENC_STRICT, &zstat))
	{
	  err = -EIO;
	  goto end;
	}
      guint last = strlen (zstat.name) - 1;
      if (zstat.name[last] == '/')
	{
	  unit_name = g_strdup (zstat.name);
	  unit_name[last] = 0;
	  break;
	}
    }

  snprintf (entry_name, PATH_MAX, "%s/%s", unit_name, UNIT_MANIFEST_FILE);
  unit_manifest = zip_fopen (unit, entry_name, 0);
  if (!unit_manifest)
    {
      err = -EIO;
      goto end;
    }
  if (zip_stat (unit, entry_name, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }
  manifest = g_byte_array_sized_new (zstat.size);
  g_byte_array_set_size (manifest, zstat.size);
  if (zip_fread (unit_manifest, manifest->data, zstat.size) != zstat.size)
    {
      err = -EIO;
      goto end;
    }
  debug_print (1, "%s read (%d B)", UNIT_MANIFEST_FILE, manifest->len);

  if ((err = logue_get_manifest (manifest, &logue_manifest)))
    {
      goto end;
    }

  snprintf (entry_name, PATH_MAX, "%s/%s", unit_name, UNIT_PAYLOAD_FILE);
  unit_payload = zip_fopen (unit, entry_name, 0);
  if (!unit_payload)
    {
      err = -EIO;
      goto end;
    }
  if (zip_stat (unit, entry_name, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }
  payload = g_byte_array_sized_new (zstat.size);
  g_byte_array_set_size (payload, zstat.size);
  if (zip_fread (unit_payload, payload->data, zstat.size) != zstat.size)
    {
      err = -EIO;
      goto end;
    }
  debug_print (1, "%s read (%d B)", UNIT_PAYLOAD_FILE, payload->len);

  // Payload header
  len = payload->len + 0x484;
  msg_payload_8bit = g_byte_array_sized_new (len + 8);
  msg_payload_8bit->len = len + 8;
  memset (msg_payload_8bit->data, 0, len + 8);
  v = GUINT32_TO_LE (len);
  memcpy (&msg_payload_8bit->data[0], &v, sizeof (guint32));

  //Payload-JSON related headers
  msg_payload_8bit->data[8] = logue_manifest.status.module_id;
  msg_payload_8bit->data[9] = logue_manifest.status.platform_id;
  msg_payload_8bit->data[10] = 0;	//Reserved byte
  msg_payload_8bit->data[11] = logue_manifest.status.api_version.major;
  msg_payload_8bit->data[12] = logue_manifest.status.api_version.minor;
  msg_payload_8bit->data[13] = logue_manifest.status.api_version.patch;
  v = GUINT32_TO_LE (logue_manifest.status.developer_id);
  memcpy (&msg_payload_8bit->data[14], &v, sizeof (guint32));
  v = GUINT32_TO_LE (logue_manifest.status.program_id);
  memcpy (&msg_payload_8bit->data[18], &v, sizeof (guint32));
  msg_payload_8bit->data[22] = 0;	//Reserved byte
  msg_payload_8bit->data[22] = logue_manifest.status.program_version.patch;
  msg_payload_8bit->data[23] = logue_manifest.status.program_version.minor;
  msg_payload_8bit->data[24] = logue_manifest.status.program_version.major;
  memcpy (&msg_payload_8bit->data[26], logue_manifest.status.name,
	  LOGUE_SLOT_STATUS_NAME_LEN);
  msg_payload_8bit->data[40] = logue_manifest.param_num;
  for (gint i = 0; i < logue_manifest.param_num; i++)
    {
      msg_payload_8bit->data[44 + i * 16] = logue_manifest.parameters[i].min;
      msg_payload_8bit->data[45 + i * 16] = logue_manifest.parameters[i].max;
      msg_payload_8bit->data[46 + i * 16] = logue_manifest.parameters[i].type;
      memcpy (&msg_payload_8bit->data[47 + i * 16],
	      logue_manifest.parameters[i].name,
	      LOGUE_SLOT_STATUS_PARAM_NAME_LEN);
    }

  // Payload-binary data
  v = GUINT32_TO_LE (payload->len);
  memcpy (&msg_payload_8bit->data[0x404], &v, sizeof (guint32));
  memcpy (&msg_payload_8bit->data[0x408], payload->data, payload->len);

  // CRC32
  crc = crc32 (0, &msg_payload_8bit->data[8], len);
  debug_print (2, "CRC (%d B): %0x", len, crc);
  v = GUINT32_TO_LE (crc);
  memcpy (&msg_payload_8bit->data[4], &v, sizeof (guint32));

  guint midi_len = common_8bit_msg_to_midi_msg_size (msg_payload_8bit->len);
  msg_payload_midi = g_byte_array_sized_new (2 + midi_len + 1);
  msg_payload_midi->data[0] = logue_manifest.status.module_id;
  msg_payload_midi->data[1] = 0;	// slot
  msg_payload_midi->len += 2 + midi_len + 1;
  common_8bit_msg_to_midi_msg (msg_payload_8bit->data,
			       &msg_payload_midi->data[2],
			       msg_payload_8bit->len);
  msg_payload_midi->data[2 + midi_len] = 0;

  // first byte is the device
  msg = logue_get_msg (0, 0x4a, msg_payload_midi->data,
		       msg_payload_midi->len);
  name = g_path_get_basename (path);
  filename_remove_ext (name);
  idata_init (sysex, msg, name, NULL, NULL);
  free_msg (msg_payload_midi);
  free_msg (msg_payload_8bit);

end:
  g_free (unit_name);
  if (unit_manifest)
    {
      zip_fclose (unit_manifest);
      free_msg (manifest);
    }
  if (unit_payload)
    {
      zip_fclose (unit_payload);
      free_msg (payload);
    }
  err = zip_close (unit) ? -EIO : err;
  return err;
}

static gint
logue_upload (struct backend *backend, const gchar *path,
	      struct idata *sysex, struct task_control *control,
	      enum logue_module module)
{
  gint err;
  guint id;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data = backend->data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  if (sysex->content->data[7] != module)
    {
      return -EINVAL;
    }

  tx_msg = g_byte_array_sized_new (sysex->content->len);
  g_byte_array_append (tx_msg, sysex->content->data, sysex->content->len);
  tx_msg->data[5] = logue_data->device;
  tx_msg->data[8] = id;
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }
  if (!LOGUE_CHECK_STATUS_MSG_COMPLETED (rx_msg))
    {
      err = -EIO;
    }

  free_msg (rx_msg);

  usleep (LOGUE_REST_TIME_US);

  return err;
}

static gint
logue_osc_upload (struct backend *backend, const gchar *path,
		  struct idata *sysex, struct task_control *control)
{
  return logue_upload (backend, path, sysex, control, FS_LOGUE_MODULE_OSC);
}

static gint
logue_modfx_upload (struct backend *backend, const gchar *path,
		    struct idata *sysex, struct task_control *control)
{
  return logue_upload (backend, path, sysex, control, FS_LOGUE_MODULE_MODFX);
}

static gint
logue_delfx_upload (struct backend *backend, const gchar *path,
		    struct idata *sysex, struct task_control *control)
{
  return logue_upload (backend, path, sysex, control, FS_LOGUE_MODULE_DELFX);
}

static gint
logue_revfx_upload (struct backend *backend, const gchar *path,
		    struct idata *sysex, struct task_control *control)
{
  return logue_upload (backend, path, sysex, control, FS_LOGUE_MODULE_REVFX);
}

static gint
logue_download (struct backend *backend, const gchar *path,
		struct idata *data, struct task_control *control,
		enum logue_module module)
{
  guint id;
  gint err;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data = backend->data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  tx_msg = logue_get_msg_op_type_id (logue_data->device, 0x1a, module, id);
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      return err;
    }

  // Empty slot
  if (rx_msg->len == 10)
    {
      free_msg (rx_msg);
      err = -EINVAL;
    }
  else
    {
      // This fix is required as the received message has an additional byte at position 9.
      memcpy (&rx_msg->data[9], &rx_msg->data[10], rx_msg->len - 11);
      rx_msg->data[rx_msg->len - 2] = 0;	// Last data byte set as in logue_unit_load function.

      idata_init (data, rx_msg, NULL, NULL, NULL);
    }

  usleep (LOGUE_REST_TIME_US);

  return err;
}

static gint
logue_osc_download (struct backend *backend, const gchar *path,
		    struct idata *oscillator, struct task_control *control)
{
  return logue_download (backend, path, oscillator, control,
			 FS_LOGUE_MODULE_OSC);
}

static gint
logue_modfx_download (struct backend *backend, const gchar *path,
		      struct idata *oscillator, struct task_control *control)
{
  return logue_download (backend, path, oscillator, control,
			 FS_LOGUE_MODULE_MODFX);
}

static gint
logue_delfx_download (struct backend *backend, const gchar *path,
		      struct idata *oscillator, struct task_control *control)
{
  return logue_download (backend, path, oscillator, control,
			 FS_LOGUE_MODULE_DELFX);
}

static gint
logue_revfx_download (struct backend *backend, const gchar *path,
		      struct idata *oscillator, struct task_control *control)
{
  return logue_download (backend, path, oscillator, control,
			 FS_LOGUE_MODULE_REVFX);
}

static gint
logue_load (struct backend *backend, const gchar *path, struct idata *sysex,
	    struct task_control *control)
{
  const gchar *ext;

  ext = filename_get_ext (path);
  if (strcmp (ext, BE_SYSEX_EXT))
    {
      return logue_unit_load (path, sysex, control);
    }
  else
    {
      return file_load (path, sysex, control);
    }
}

static const struct fs_operations FS_LOGUE_OSC_OPERATIONS = {
  .id = FS_LOGUE_MODULE_OSC,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "oscillator",
  .gui_name = "Oscillators",
  .gui_icon = FS_ICON_OSCILLATOR,
  .file_icon = FS_ICON_OSCILLATOR,
  .readdir = logue_osc_read_dir,
  .print_item = common_print_item,
  .delete = logue_osc_clear,
  .download = logue_osc_download,
  .upload = logue_osc_upload,
  .load = logue_load,
  .save = file_save,
  .get_slot = logue_get_id_as_slot,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nn
};

static const struct fs_operations FS_LOGUE_MODFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_MODFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "modulation",
  .gui_name = "Modulation FX",
  .gui_icon = FS_ICON_FX_MODULATION,
  .file_icon = FS_ICON_FX_MODULATION,
  .readdir = logue_modfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_modfx_clear,
  .download = logue_modfx_download,
  .upload = logue_modfx_upload,
  .load = logue_load,
  .save = file_save,
  .get_slot = logue_get_id_as_slot,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_nn
};

static const struct fs_operations FS_LOGUE_DELFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_DELFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "delay",
  .gui_name = "Delay FX",
  .gui_icon = FS_ICON_FX_DELAY,
  .file_icon = FS_ICON_FX_DELAY,
  .readdir = logue_delfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_delfx_clear,
  .download = logue_delfx_download,
  .upload = logue_delfx_upload,
  .load = logue_load,
  .save = file_save,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_n
};

static const struct fs_operations FS_LOGUE_REVFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_REVFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = "reverb",
  .gui_name = "Reverb FX",
  .gui_icon = FS_ICON_FX_REVERB,
  .file_icon = FS_ICON_FX_REVERB,
  .readdir = logue_revfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_revfx_clear,
  .download = logue_revfx_download,
  .upload = logue_revfx_upload,
  .load = logue_load,
  .save = file_save,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_slot_get_download_path_n
};

// This message is not really useful but it's part of what logue-cli does.
// This does not seem to work with the Minilogue XD.
enum logue_device
logue_get_user_api_request_nts1 (struct backend *backend,
				 enum logue_device device)
{
  GByteArray *tx_msg, *rx_msg;
  enum logue_platform platform;
  struct logue_version api_version;

  tx_msg = logue_get_msg (device, 0x17, NULL, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -ENODEV;
    }
  if (rx_msg->len != 12 || LOGUE_GET_MSG_OP (rx_msg) != 0x47)
    {
      free_msg (rx_msg);
      return -EIO;
    }

  platform = rx_msg->data[7];

  if (platform != LOGUE_PLATFORM_NTS1)
    {
      error_print ("Unexpected platform %d", platform);
    }

  api_version.major = rx_msg->data[8];
  api_version.minor = rx_msg->data[9];
  api_version.patch = rx_msg->data[10];

  debug_print (2, "Platform ID: %d", platform);
  debug_print (2, "Major: %d", api_version.major);
  debug_print (2, "Minor: %d", api_version.minor);
  debug_print (2, "Patch: %d", api_version.patch);

  free_msg (rx_msg);
  return platform;
}

static gint
logue_handshake (struct backend *backend)
{
  gint err = 0;
  const gchar *name;
  struct logue_data *data;
  enum logue_device device;
  GByteArray *tx_msg, *rx_msg;
  enum logue_platform platform;

  if (memcmp (backend->midi_info.company, KORG_ID, sizeof (KORG_ID)))
    {
      return -ENODEV;
    }

  tx_msg = g_byte_array_sized_new (sizeof (LOGUE_SEARCH_DEVICE));
  g_byte_array_append (tx_msg, LOGUE_SEARCH_DEVICE,
		       sizeof (LOGUE_SEARCH_DEVICE));
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -ENODEV;
    }

  if (logue_validate_device (rx_msg, LOGUE_DEVICE_PROLOGUE))
    {
      name = LOGUE_NAME_PROLOGUE;
      device = LOGUE_DEVICE_PROLOGUE;
      platform = LOGUE_PLATFORM_PROLOGUE;
    }
  else if (logue_validate_device (rx_msg, LOGUE_DEVICE_MINILOGUE_XD))
    {
      name = LOGUE_NAME_MINILOGUE_XD;
      device = LOGUE_DEVICE_MINILOGUE_XD;
      platform = LOGUE_PLATFORM_MINILOGUE_XD;
    }
  else if (logue_validate_device (rx_msg, LOGUE_DEVICE_NTS1))
    {
      name = LOGUE_NAME_NTS1;
      device = LOGUE_DEVICE_NTS1;
      platform = logue_get_user_api_request_nts1 (backend, device);
    }
  else
    {
      free_msg (rx_msg);
      return -ENODEV;
    }

  free_msg (rx_msg);

  data = g_malloc (sizeof (struct logue_data));
  data->device = device;
  data->platform = platform;

  backend->data = data;
  backend->destroy_data = backend_destroy_data;

  gslist_fill (&backend->fs_ops, &FS_LOGUE_OSC_OPERATIONS,
	       &FS_LOGUE_MODFX_OPERATIONS,
	       &FS_LOGUE_DELFX_OPERATIONS, &FS_LOGUE_REVFX_OPERATIONS, NULL);
  snprintf (backend->name, LABEL_MAX, "KORG %s", name);

  return err;
}

const struct connector CONNECTOR_LOGUE = {
  .name = "logue",
  .handshake = logue_handshake,
  .options = 0,
  .regex = ".*(prologue|minilogue xd|NTS-1).*"
};
