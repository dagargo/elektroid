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
// The parameter name is 13 bytes including the null byte at the end. However, there are a few caveats.
// * The parameter name in the manifest.json migh be longer than 12 chars.
// * The SysEx format only has 13 usable bytes.
// * `logue-cli` might use up to 13 chars without using the null char at the end. Both the transfer and the unit inside the NTS-1 work.
// * The KORG NTS-1 shows up to 12 characters.
// Considering all this, the connector always includes the null char at the end.
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

#define LOGUE_MAX_UNIT_SIZE (1 * MI)

struct logue_version
{
  guint8 reserved;
  guint8 major;
  guint8 minor;
  guint8 patch;
};

union logue_version_bin
{
  struct logue_version version;
  guint32 bin;
};

struct logue_data
{
  guint8 device;
  guint8 channel;
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
  gint8 min;
  gint8 max;
  enum logue_slot_parameter_type type;
  gchar name[LOGUE_SLOT_STATUS_PARAM_NAME_LEN];
};

// PLATFORM ID and MODULE ID seem to be swapped in the official documentation.
// The reserved bytes in API and program version seem to not be in the position explained in the official documentation.
// api_version and program_version have different byte order.
struct logue_slot_status
{
  guint8 module_id;
  guint8 platform_id;
  union logue_version_bin api_version;
  guint32 developer_id;
  guint32 program_id;
  union logue_version_bin program_version;
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

#define FS_LOGUE_MODULE_MODFX_NAME "modfx"
#define FS_LOGUE_MODULE_DELFX_NAME "delfx"
#define FS_LOGUE_MODULE_REVFX_NAME "revfx"
#define FS_LOGUE_MODULE_OSC_NAME "osc"

static const guint8 MSG_HEADER[] = { 0xf0, 0x42, 0x30, 0, 1 };

static const guint8 LOGUE_SEARCH_DEVICE[] =
  { 0xf0, 0x42, 0x50, 0, 0x55, 0xf7 };

static const gchar *LOGUE_NTS1_EXTS[] = { "ntkdigunit", NULL };
static const gchar *LOGUE_PROLOGUE_EXTS[] = { "prlgunit", NULL };
static const gchar *LOGUE_MINILOGUEXD_EXTS[] = { "mnlgxdunit", NULL };

static const gchar *
logue_get_platform_name (guint8 platform_id)
{
  switch (platform_id)
    {
    case LOGUE_PLATFORM_PROLOGUE:
      return LOGUE_PLATFORM_NAME_PROLOGUE;
    case LOGUE_PLATFORM_MINILOGUE_XD:
      return LOGUE_PLATFORM_NAME_MINILOGUE_XD;
    case LOGUE_PLATFORM_NTS1:
      return LOGUE_PLATFORM_NAME_NTS1;
    default:
      return NULL;
    }
}

static const gchar *
logue_get_module_name (guint8 module_id)
{
  switch (module_id)
    {
    case FS_LOGUE_MODULE_MODFX:
      return FS_LOGUE_MODULE_MODFX_NAME;
    case FS_LOGUE_MODULE_DELFX:
      return FS_LOGUE_MODULE_DELFX_NAME;
    case FS_LOGUE_MODULE_REVFX:
      return FS_LOGUE_MODULE_REVFX_NAME;
    case FS_LOGUE_MODULE_OSC:
      return FS_LOGUE_MODULE_OSC_NAME;
    default:
      return NULL;
    }
}

static void
logue_set_version_str (struct logue_version *version, gchar *out, guint size)
{
  snprintf (out, size, "%d.%d-%d", version->major, version->minor,
	    version->patch);
}

static gboolean
logue_validate_device (GByteArray *msg, guint8 device_id)
{
  return (msg->data[6] == device_id && msg->data[7] == 1 &&
	  msg->data[8] == 0 && msg->data[9] == 0);
}

static GByteArray *
logue_get_msg (struct logue_data *logue_data, guint8 op, const guint8 *data,
	       guint len)
{
  GByteArray *msg;

  msg = g_byte_array_sized_new (sizeof (MSG_HEADER) + 3 + len);
  g_byte_array_append (msg, MSG_HEADER, sizeof (MSG_HEADER));
  msg->data[2] |= logue_data->channel;
  g_byte_array_append (msg, &logue_data->device, 1);
  g_byte_array_append (msg, &op, 1);
  if (data && len > 0)
    {
      g_byte_array_append (msg, data, len);
    }
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  return msg;
}

static GByteArray *
logue_get_msg_op_type (struct logue_data *logue_data, guint8 op,
		       guint8 module)
{
  return logue_get_msg (logue_data, op, &module, 1);
}

static GByteArray *
logue_get_msg_op_type_id (struct logue_data *logue_data, guint8 op,
			  guint8 module, guint8 slot)
{
  guint8 data[2];
  data[0] = module;
  data[1] = slot;
  return logue_get_msg (logue_data, op, data, 2);
}

static gint
logue_next_dentry (struct item_iterator *iter)
{
  guint rx_msg_len;
  GByteArray *tx_msg, *rx_msg;
  struct logue_iter_data *iter_data = iter->data;
  struct logue_data *logue_data = iter_data->backend->data;

  if (iter_data->next >= iter_data->slots)
    {
      return -ENOENT;
    }

  tx_msg = logue_get_msg_op_type_id (logue_data, 0x19,
				     iter_data->module, iter_data->next);
  rx_msg = backend_tx_and_rx_sysex (iter_data->backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  if (logue_data->device == LOGUE_DEVICE_NTS1)
    {
      rx_msg_len = 53;
    }
  else
    {
      rx_msg_len = 48;
    }

  if (rx_msg->len == rx_msg_len && LOGUE_GET_MSG_OP (rx_msg) == 0x49)
    {
      gsize size;
      GByteArray *content;

      // USER SLOT STATUS REQUEST
      // There is an unknown byte before the payload that is not in the official documentation.
      // 7 bit payload is 37.
      // 8 bit payload is 32.
      // In the case of the NTS-1, there are 5 additional data bytes (the last 4 set to 0) at the end.
      // logue        (53): f0 42 30 00 01 57 49 04 01 00 00 04 03 00 01 01 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 66 6d 34 00 6f 00 00 00 00 00 00 00 00 00 00 00 06 00 00 00 00 f7
      // Minilogue XD (48): f0 42 3b 00 01 51 49 04 01 00 00 04 01 00 00 01 00 00 00 00 00 00 00 00 00 00 00 01 00 01 00 53 6f 75 00 70 65 72 00 00 00 00 00 00 00 00 00 f7

      size = common_midi_msg_to_8bit_msg_size (37);
      content = g_byte_array_sized_new (size);
      content->len = size;
      common_midi_msg_to_8bit_msg (&rx_msg->data[10], content->data, 37);

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
  guint rx_msg_len;
  GByteArray *tx_msg, *rx_msg;
  struct logue_iter_data *data;
  struct logue_data *logue_data = backend->data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  tx_msg = logue_get_msg_op_type (logue_data, 0x18, module);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -ENODEV;
    }

  if (logue_data->device == LOGUE_DEVICE_NTS1)
    {
      rx_msg_len = 24;
    }
  else
    {
      rx_msg_len = 21;
    }

  if (rx_msg->len == rx_msg_len && LOGUE_GET_MSG_OP (rx_msg) == 0x48)
    {
      guint size;
      GByteArray *content;
      guint32 storage_size, load_size, slot_count;

      // USER MODULE INFO
      // There are 2 unknown bytes before the payload that is not in the official documentation.
      // 7 bit payload is 11.
      // 8 bit payload is 9.
      // In the case of the NTS-1, there are 3 additional data bytes set to 0 at the end.
      // logue        (24): f0 42 30 00 01 57 48 04 00 23 70 0f 00 00 00 00 00 00 00 10 00 00 00 f7
      // Minilogue XD (21): f0 42 3b 00 01 51 48 04 00 23 70 0f 00 00 00 00 00 00 00 10 f7

      size = common_midi_msg_to_8bit_msg_size (11);
      content = g_byte_array_sized_new (size);
      content->len = size;
      common_midi_msg_to_8bit_msg (&rx_msg->data[9], content->data, 11);

      if (debug_level > 1)
	{
	  gchar *text = debug_get_hex_data (debug_level, content->data,
					    content->len);
	  debug_print (1, "Message received (%u): %s", content->len, text);
	  g_free (text);
	}

      storage_size = GUINT32_FROM_LE (*((guint32 *) (&content->data[0])));
      load_size = GUINT32_FROM_LE (*((guint32 *) (&content->data[4])));
      slot_count = content->data[8];

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

  tx_msg = logue_get_msg_op_type_id (logue_data, 0x1b, module, id);
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

static gint
logue_get_manifest_from_json (struct logue_manifest *manifest,
			      GByteArray *manifest_json)
{
  gint err;
  gchar *aux;
  GError *error;
  JsonParser *parser;
  JsonReader *reader;
  const gchar *string;
  gchar version[LABEL_MAX];

  err = 0;

  parser = json_parser_new ();
  error = NULL;
  if (!json_parser_load_from_data (parser, (gchar *) manifest_json->data,
				   manifest_json->len, &error))
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
  manifest->status.api_version.version.major = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.api_version.version.minor = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.api_version.version.patch = strtol (string, &aux, 10);
  logue_set_version_str (&manifest->status.api_version.version, version,
			 LABEL_MAX);
  debug_print (2, "API: %s", version);
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
  manifest->status.program_version.version.major = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.program_version.version.minor = strtol (string, &aux, 10);
  string = aux + 1;
  manifest->status.program_version.version.patch = strtol (string, &aux, 10);
  logue_set_version_str (&manifest->status.program_version.version, version,
			 LABEL_MAX);
  debug_print (2, "Version: %s", version);
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
		  LOGUE_SLOT_STATUS_PARAM_NAME_LEN);
	  snprintf (manifest->parameters[i].name,
		    LOGUE_SLOT_STATUS_PARAM_NAME_LEN, "%s", string);
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

	  debug_print (2, "Parameter '%s' read",
		       manifest->parameters[i].name);

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

// Complete message: f0 42 30 00 01 57 4a MM SS PP .. PP 00 f7
// SysEx header
// 1 byte for the module
// 1 byte for the slot
// MIDI encoded payload
// null byte
// SysEx end

// Decoded payload
// manifest.json related content (400 B)
// 00000000 54 08 00 00 d2 46 95 8a 04 03 00 01 01 00 00 00 -> total size (u32), crc (u32), module (u8), platform (u8), 0 (u8), version
// 00000010 00 00 00 00 00 00 00 01 00 00 6d 61 73 73 00 00 -> version... At 26 (0x1a), name (14 chars),
// 00000020 00 00 00 00 00 00 00 00 02 00 00 00 00 06 02 56 -> name... At 40 (0x28), num params. At 41, params (blocks of 0 0 0 min max type name (9 chars))
// 00000030 6f 69 63 65 73 00 00 00 00 00 00 00 00 64 00 42
// 00000040 65 61 74 69 6e 67 00 00 00 00 00 00 00 00 00 00
// [...]
// payload.bin
// 00000400 00 00 00 00 f8 04 00 00 55 4d 4f 44 00 01 01 00 -> 0 (u32), payload size (u32), payload

gint
logue_get_sysex_from_unit (struct idata *sysex, struct idata *unit,
			   struct task_control *control, guint8 device,
			   guint8 channel, guint8 slot)
{
  gint err;
  zip_t *zip;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_source_t *zip_source;
  zip_int64_t entries;
  guint32 v, len, crc, full_len;
  gchar *unit_name = NULL;
  gchar entry_name[PATH_MAX];
  struct logue_data logue_data;
  struct logue_manifest logue_manifest;
  zip_file_t *unit_payload = NULL;
  zip_file_t *unit_manifest = NULL;
  GByteArray *msg, *manifest = NULL, *payload =
    NULL, *msg_payload_midi, *msg_payload_8bit;

  zip_error_init (&zerror);
  zip_source = zip_source_buffer_create (unit->content->data,
					 unit->content->len, 0, &zerror);
  if (!zip_source)
    {
      error_print ("Error while creating zip source: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  zip = zip_open_from_source (zip_source, ZIP_RDONLY, &zerror);
  if (!zip)
    {
      error_print ("Error while creating in memory zip: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (zip_source);
      return -1;
    }

  entries = zip_get_num_entries (zip, 0);
  if (entries != 3)
    {
      err = -EIO;
      goto end;
    }
  for (guint i = 0; i < 3; i++)
    {
      if (zip_stat_index (zip, i, ZIP_FL_ENC_STRICT, &zstat))
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
  unit_manifest = zip_fopen (zip, entry_name, 0);
  if (!unit_manifest)
    {
      err = -EIO;
      goto end;
    }
  if (zip_stat (zip, entry_name, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }
  manifest = g_byte_array_sized_new (zstat.size);
  manifest->len = zstat.size;
  if (zip_fread (unit_manifest, manifest->data, zstat.size) != zstat.size)
    {
      err = -EIO;
      goto end;
    }
  debug_print (1, "%s read (%d B)", UNIT_MANIFEST_FILE, manifest->len);

  err = logue_get_manifest_from_json (&logue_manifest, manifest);
  if (err)
    {
      goto end;
    }

  snprintf (entry_name, PATH_MAX, "%s/%s", unit_name, UNIT_PAYLOAD_FILE);
  unit_payload = zip_fopen (zip, entry_name, 0);
  if (!unit_payload)
    {
      err = -EIO;
      goto end;
    }
  if (zip_stat (zip, entry_name, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }
  payload = g_byte_array_sized_new (zstat.size);
  payload->len = zstat.size;
  if (zip_fread (unit_payload, payload->data, zstat.size) != zstat.size)
    {
      err = -EIO;
      goto end;
    }
  debug_print (1, "%s read (%d B)", UNIT_PAYLOAD_FILE, payload->len);

  len = 0x484 + payload->len;
  full_len = 8 + len;
  msg_payload_8bit = g_byte_array_sized_new (full_len);
  msg_payload_8bit->len = full_len;
  memset (msg_payload_8bit->data, 0, full_len);

  v = GUINT32_TO_LE (len);
  memcpy (&msg_payload_8bit->data[0], &v, sizeof (guint32));

  // manifest.json related headers
  msg_payload_8bit->data[8] = logue_manifest.status.module_id;
  msg_payload_8bit->data[9] = logue_manifest.status.platform_id;

  memcpy (&msg_payload_8bit->data[10], &logue_manifest.status.api_version.bin,
	  sizeof (union logue_version_bin));

  v = GUINT32_TO_LE (logue_manifest.status.developer_id);
  memcpy (&msg_payload_8bit->data[14], &v, sizeof (guint32));

  v = GUINT32_TO_LE (logue_manifest.status.program_id);
  memcpy (&msg_payload_8bit->data[18], &v, sizeof (guint32));

  v = GUINT32_SWAP_LE_BE (logue_manifest.status.program_version.bin);
  memcpy (&msg_payload_8bit->data[22], &v, sizeof (union logue_version_bin));

  memcpy (&msg_payload_8bit->data[26], logue_manifest.status.name,
	  LOGUE_SLOT_STATUS_NAME_LEN);

  msg_payload_8bit->data[40] = logue_manifest.param_num;
  for (gint i = 0; i < logue_manifest.param_num; i++)
    {
      debug_print (2, "Adding parameter '%s'...",
		   logue_manifest.parameters[i].name);
      msg_payload_8bit->data[44 + i * 16] = logue_manifest.parameters[i].min;
      msg_payload_8bit->data[45 + i * 16] = logue_manifest.parameters[i].max;
      msg_payload_8bit->data[46 + i * 16] = logue_manifest.parameters[i].type;
      memcpy (&msg_payload_8bit->data[47 + i * 16],
	      logue_manifest.parameters[i].name,
	      LOGUE_SLOT_STATUS_PARAM_NAME_LEN);
    }

  // payload.bin
  v = GUINT32_TO_LE (payload->len);
  memcpy (&msg_payload_8bit->data[0x404], &v, sizeof (guint32));
  memcpy (&msg_payload_8bit->data[0x408], payload->data, payload->len);

  // CRC32
  crc = crc32 (0, &msg_payload_8bit->data[8], len);
  debug_print (2, "CRC (%d B): %0x", len, crc);
  v = GUINT32_TO_LE (crc);
  memcpy (&msg_payload_8bit->data[4], &v, sizeof (guint32));

  guint midi_len = common_8bit_msg_to_midi_msg_size (msg_payload_8bit->len);
  full_len = 2 + midi_len + 1;
  msg_payload_midi = g_byte_array_sized_new (full_len);
  msg_payload_midi->len = full_len;
  msg_payload_midi->data[0] = logue_manifest.status.module_id;
  msg_payload_midi->data[1] = 0;	// slot
  common_8bit_msg_to_midi_msg (msg_payload_8bit->data,
			       &msg_payload_midi->data[2],
			       msg_payload_8bit->len);
  msg_payload_midi->data[full_len - 1] = 0;	// null byte

  logue_data.device = device;
  logue_data.channel = channel;

  // first byte is the device
  msg = logue_get_msg (&logue_data, 0x4a, msg_payload_midi->data,
		       msg_payload_midi->len);
  idata_init (sysex, msg, strdup (unit_name), NULL, NULL);
  free_msg (msg_payload_midi);
  free_msg (msg_payload_8bit);

  if (sysex->content->data[7] != logue_manifest.status.module_id)
    {
      return -EINVAL;
    }

  sysex->content->data[5] = device;
  sysex->content->data[8] = slot;

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
  err = zip_close (zip) ? -EIO : err;
  return err;
}

static gint
logue_upload (struct backend *backend, const gchar *path,
	      struct idata *unit, struct task_control *control,
	      enum logue_module module)
{
  gint err;
  guint id;
  struct idata sysex;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data = backend->data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  err = logue_get_sysex_from_unit (&sysex, unit, control, logue_data->device,
				   logue_data->channel, id);
  if (err)
    {
      return err;
    }
  tx_msg = idata_steal (&sysex);
  err = common_data_tx_and_rx (backend, tx_msg, &rx_msg, control);
  if (err)
    {
      goto end;
    }
  if (!LOGUE_CHECK_STATUS_MSG_COMPLETED (rx_msg))
    {
      err = -EIO;
    }

  free_msg (rx_msg);

  usleep (LOGUE_REST_TIME_US);

end:
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
logue_get_json_from_manifest (GByteArray *manifest_json,
			      struct logue_manifest *manifest)
{
  guint len;
  gchar *json;
  JsonNode *root;
  JsonGenerator *gen;
  JsonBuilder *builder;
  gchar version[LABEL_MAX];
  const gchar *platform_name, *module_name;

  platform_name = logue_get_platform_name (manifest->status.platform_id);
  if (!platform_name)
    {
      return -1;
    }

  module_name = logue_get_module_name (manifest->status.module_id);
  if (!module_name)
    {
      return -1;
    }

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_HEADER);
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_PLATFORM);
  json_builder_add_string_value (builder, platform_name);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_MODULE);
  json_builder_add_string_value (builder, module_name);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_API);
  logue_set_version_str (&manifest->status.api_version.version, version,
			 LABEL_MAX);
  json_builder_add_string_value (builder, version);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_DEV_ID);
  json_builder_add_int_value (builder, manifest->status.developer_id);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_PROGRAM_ID);
  json_builder_add_int_value (builder, manifest->status.program_id);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_VERSION);
  logue_set_version_str (&manifest->status.program_version.version, version,
			 LABEL_MAX);
  json_builder_add_string_value (builder, version);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_NAME);
  json_builder_add_string_value (builder, manifest->status.name);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_NUM_PARAM);
  json_builder_add_int_value (builder, manifest->param_num);

  json_builder_set_member_name (builder, LOGUE_MANIFEST_PARAMS);
  json_builder_begin_array (builder);

  for (gint i = 0; i < manifest->param_num; i++)
    {
      debug_print (2, "Adding parameter '%s'...",
		   manifest->parameters[i].name);
      json_builder_begin_array (builder);
      json_builder_add_string_value (builder, manifest->parameters[i].name);
      json_builder_add_int_value (builder, manifest->parameters[i].min);
      json_builder_add_int_value (builder, manifest->parameters[i].max);
      json_builder_add_string_value (builder,
				     manifest->parameters[i].type ==
				     LOGUE_SLOT_PARAMETER_TYPE_PERCENT ? "%" :
				     "");
      json_builder_end_array (builder);
    }

  json_builder_end_array (builder);

  json_builder_end_object (builder);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  g_object_set (gen, "pretty", TRUE, NULL);
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  len = strlen (json);
  g_byte_array_append (manifest_json, (guint8 *) json, len);
  g_free (json);

  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);

  return 0;
}

static gint
logue_unit_add_file (zip_t *zip, const gchar *dir, const gchar *name,
		     GByteArray *content)
{
  zip_int64_t index;
  zip_error_t zerror;
  zip_source_t *source;
  gchar entry_name[PATH_MAX];

  debug_print (1, "Adding '%s' to zip...", name);

  zip_error_init (&zerror);
  source = zip_source_buffer_create (content->data, content->len, 0, &zerror);
  if (!source)
    {
      error_print ("Error while creating '%s' source: %s",
		   name, zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  snprintf (entry_name, PATH_MAX, "%s/%s", dir, name);
  index = zip_file_add (zip, entry_name, source,
			ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
  if (index < 0)
    {
      error_print ("Error while adding '%s': %s", name,
		   zip_error_strerror (zip_get_error (zip)));
      zip_source_free (source);
      return -1;
    }

  zip_file_set_mtime (zip, index, 0, 0);

  return 0;
}

// Complete message: f0 42 30 00 01 57 4a MM SS 00 PP .. PP f7. null byte at 9th byte instead of at len - 2 like in the sent message.
// SysEx header
// 1 byte for the module
// 1 byte for the slot
// 1 null byte
// MIDI encoded payload
// null byte
// SysEx end

gint
logue_get_unit_from_sysex (struct idata *unit, struct idata *sysex,
			   struct task_control *control)
{
  gint err;
  guint32 v;
  zip_t *zip;
  gchar *buff;
  zip_stat_t zstat;
  zip_int64_t index;
  zip_error_t zerror;
  gchar name[LABEL_MAX];
  const gchar *module_name;
  zip_source_t *zip_source;
  guint size, input_size, expected_size;
  struct logue_manifest logue_manifest;
  GByteArray *msg_payload_8bit = NULL, *payload = NULL, *manifest =
    NULL, *content;

  // We can extract the data in just one pass.
  input_size = sysex->content->len - 11;
  size = common_midi_msg_to_8bit_msg_size (input_size);
  msg_payload_8bit = g_byte_array_sized_new (size);
  msg_payload_8bit->len = size;
  common_midi_msg_to_8bit_msg (&sysex->content->data[10],
			       msg_payload_8bit->data, input_size);

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_data (debug_level, msg_payload_8bit->data,
					msg_payload_8bit->len);
      debug_print (2, "8 bit message (%u): %s", msg_payload_8bit->len, text);
      g_free (text);
    }

  memcpy (&v, &msg_payload_8bit->data[0], sizeof (guint32));
  expected_size = GUINT32_FROM_LE (v);
  size = msg_payload_8bit->len - 8;
  if (size != expected_size)
    {
      error_print ("Unexpected 8 bit payload size (%d != %d)", size,
		   expected_size);
      err = -1;
      goto end;
    }

  // The CRC value seems to be different than the one calculated in logue_get_sysex_from_unit.
  // We skip the check of the CRC entirely.

  // manifest.json related headers
  logue_manifest.status.module_id = msg_payload_8bit->data[8];
  logue_manifest.status.platform_id = msg_payload_8bit->data[9];

  memcpy (&logue_manifest.status.api_version, &msg_payload_8bit->data[10],
	  sizeof (union logue_version_bin));

  memcpy (&v, &msg_payload_8bit->data[14], sizeof (guint32));
  logue_manifest.status.developer_id = GUINT32_FROM_LE (v);

  memcpy (&v, &msg_payload_8bit->data[18], sizeof (guint32));
  logue_manifest.status.program_id = GUINT32_FROM_LE (v);

  memcpy (&v, &msg_payload_8bit->data[22], sizeof (union logue_version_bin));
  logue_manifest.status.program_version.bin = GUINT32_SWAP_LE_BE (v);

  memcpy (logue_manifest.status.name, &msg_payload_8bit->data[26],
	  LOGUE_SLOT_STATUS_NAME_LEN);

  logue_manifest.param_num = msg_payload_8bit->data[40];
  for (gint i = 0; i < logue_manifest.param_num; i++)
    {
      logue_manifest.parameters[i].min = msg_payload_8bit->data[44 + i * 16];
      logue_manifest.parameters[i].max = msg_payload_8bit->data[45 + i * 16];
      logue_manifest.parameters[i].type = msg_payload_8bit->data[46 + i * 16];
      snprintf (logue_manifest.parameters[i].name,
		LOGUE_SLOT_STATUS_PARAM_NAME_LEN, "%s",
		(gchar *) & msg_payload_8bit->data[47 + i * 16]);

      debug_print (2, "Parameter '%s' read",
		   logue_manifest.parameters[i].name);
    }

  module_name = logue_get_module_name (logue_manifest.status.module_id);
  if (!module_name)
    {
      err = -1;
      goto end;
    }

  manifest = g_byte_array_new ();
  err = logue_get_json_from_manifest (manifest, &logue_manifest);
  if (err)
    {
      free_msg (manifest);
      goto end;
    }

  // payload.bin
  memcpy (&v, &msg_payload_8bit->data[0x404], sizeof (guint32));
  size = GUINT32_FROM_LE (v);
  payload = g_byte_array_sized_new (size);
  payload->len = size;
  memcpy (payload->data, &msg_payload_8bit->data[0x408], size);

  if (debug_level > 1)
    {
      gchar *text = debug_get_hex_data (debug_level, payload->data,
					payload->len);
      debug_print (2, "Payload (%u): %s", payload->len, text);
      g_free (text);
    }

  zip_error_init (&zerror);
  buff = g_malloc (LOGUE_MAX_UNIT_SIZE);
  zip_source = zip_source_buffer_create (buff, LOGUE_MAX_UNIT_SIZE, 0,
					 &zerror);
  if (!zip_source)
    {
      error_print ("Error while creating zip source: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      g_free (buff);
      err = -1;
      goto end;
    }

  zip = zip_open_from_source (zip_source, ZIP_TRUNCATE, &zerror);
  if (!zip)
    {
      error_print ("Error while creating in memory zip: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (zip_source);
      g_free (buff);
      err = -1;
      goto end;
    }

  snprintf (name, LABEL_MAX, "%s_%s", module_name,
	    logue_manifest.status.name);
  index = zip_dir_add (zip, name, ZIP_FL_ENC_UTF_8);
  if (index < 0)
    {
      error_print ("Error while adding directory '%s': %s", name,
		   zip_error_strerror (zip_get_error (zip)));
      err = -1;
      goto end;
    }

  zip_file_set_mtime (zip, index, 0, 0);

  zip_source_keep (zip_source);

  logue_unit_add_file (zip, name, UNIT_MANIFEST_FILE, manifest);
  logue_unit_add_file (zip, name, UNIT_PAYLOAD_FILE, payload);

  zip_close (zip);

  zip_source_stat (zip_source, &zstat);
  debug_print (1, "%" PRIu64 " B written to unit", zstat.comp_size);

  zip_source_open (zip_source);
  content = g_byte_array_sized_new (zstat.comp_size);
  content->len = zstat.comp_size;
  zip_source_read (zip_source, content->data, zstat.comp_size);

  idata_init (unit, content, strdup (name), NULL, NULL);

  zip_source_close (zip_source);

end:
  if (payload)
    {
      free_msg (payload);
    }
  if (manifest)
    {
      free_msg (manifest);
    }
  free_msg (msg_payload_8bit);

  return err;
}

static gint
logue_download (struct backend *backend, const gchar *path,
		struct idata *unit, struct task_control *control,
		enum logue_module module)
{
  guint id;
  gint err;
  struct idata sysex;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data = backend->data;

  err = common_slot_get_id_from_path (path, &id);
  if (err)
    {
      return err;
    }

  tx_msg = logue_get_msg_op_type_id (logue_data, 0x1a, module, id);
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
      idata_init (&sysex, rx_msg, NULL, NULL, NULL);
      err = logue_get_unit_from_sysex (unit, &sysex, control);
      idata_clear (&sysex);
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

static const struct fs_operations FS_LOGUE_OSC_OPERATIONS = {
  .id = FS_LOGUE_MODULE_OSC,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = FS_LOGUE_MODULE_OSC_NAME,
  .gui_name = "Oscillators",
  .gui_icon = FS_ICON_OSCILLATOR,
  .file_icon = FS_ICON_OSCILLATOR,
  .readdir = logue_osc_read_dir,
  .print_item = common_print_item,
  .delete = logue_osc_clear,
  .download = logue_osc_download,
  .upload = logue_osc_upload,
  .load = common_file_load,
  .save = common_file_save,
  .get_slot = logue_get_id_as_slot,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

static const struct fs_operations FS_LOGUE_MODFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_MODFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = FS_LOGUE_MODULE_MODFX_NAME,
  .gui_name = "Modulation FX",
  .gui_icon = FS_ICON_FX_MODULATION,
  .file_icon = FS_ICON_FX_MODULATION,
  .readdir = logue_modfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_modfx_clear,
  .download = logue_modfx_download,
  .upload = logue_modfx_upload,
  .load = common_file_load,
  .save = common_file_save,
  .get_slot = logue_get_id_as_slot,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

static const struct fs_operations FS_LOGUE_DELFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_DELFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = FS_LOGUE_MODULE_DELFX_NAME,
  .gui_name = "Delay FX",
  .gui_icon = FS_ICON_FX_DELAY,
  .file_icon = FS_ICON_FX_DELAY,
  .readdir = logue_delfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_delfx_clear,
  .download = logue_delfx_download,
  .upload = logue_delfx_upload,
  .load = common_file_load,
  .save = common_file_save,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

static const struct fs_operations FS_LOGUE_REVFX_OPERATIONS = {
  .id = FS_LOGUE_MODULE_REVFX,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SHOW_SLOT_COLUMN | FS_OPTION_ALLOW_SEARCH,
  .name = FS_LOGUE_MODULE_REVFX_NAME,
  .gui_name = "Reverb FX",
  .gui_icon = FS_ICON_FX_REVERB,
  .file_icon = FS_ICON_FX_REVERB,
  .readdir = logue_revfx_read_dir,
  .print_item = common_print_item,
  .delete = logue_revfx_clear,
  .download = logue_revfx_download,
  .upload = logue_revfx_upload,
  .load = common_file_load,
  .save = common_file_save,
  .get_exts = logue_get_extensions,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_system_get_download_path
};

// This message is not really useful but it's part of what logue-cli does.
static void
logue_get_user_api_request (struct backend *backend,
			    struct logue_data *logue_data)
{
  gchar version[LABEL_MAX];
  GByteArray *tx_msg, *rx_msg;
  enum logue_platform platform;
  struct logue_version api_version;

  tx_msg = logue_get_msg (logue_data, 0x17, NULL, 0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return;
    }
  if (rx_msg->len != 12 || LOGUE_GET_MSG_OP (rx_msg) != 0x47)
    {
      free_msg (rx_msg);
      return;
    }

  platform = rx_msg->data[7];

  api_version.major = rx_msg->data[8];
  api_version.minor = rx_msg->data[9];
  api_version.patch = rx_msg->data[10];

  debug_print (2, "Platform ID: %d", platform);
  logue_set_version_str (&api_version, version, LABEL_MAX);
  debug_print (2, "Version: %s", version);

  free_msg (rx_msg);
}

static gint
logue_handshake (struct backend *backend)
{
  gint err = 0;
  const gchar *name;
  enum logue_device device;
  GByteArray *tx_msg, *rx_msg;
  struct logue_data *logue_data;

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
    }
  else if (logue_validate_device (rx_msg, LOGUE_DEVICE_MINILOGUE_XD))
    {
      name = LOGUE_NAME_MINILOGUE_XD;
      device = LOGUE_DEVICE_MINILOGUE_XD;
    }
  else if (logue_validate_device (rx_msg, LOGUE_DEVICE_NTS1))
    {
      name = LOGUE_NAME_NTS1;
      device = LOGUE_DEVICE_NTS1;
    }
  else
    {
      free_msg (rx_msg);
      return -ENODEV;
    }

  logue_data = g_malloc (sizeof (struct logue_data));
  logue_data->device = device;
  logue_data->channel = rx_msg->data[4];

  logue_get_user_api_request (backend, logue_data);

  debug_print (2, "Logue channel: 0x%02x", logue_data->channel);

  free_msg (rx_msg);

  backend->data = logue_data;
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
  .regex = ".*(prologue|minilogue xd|NTS-1).*KBD/KNOB"
};
