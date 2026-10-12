#include <string.h>
#include <errno.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../config.h"
#include "../src/sample.h"
#include "../src/utils.h"
#include "../src/connectors/elektron_pkg.h"
#include "../src/connectors/elektron.h"

gchar *elektron_get_dev_ext (struct backend *backend,
			     const struct fs_operations *ops);

const gchar **elektron_get_dev_exts (struct backend *backend,
				     const struct fs_operations *ops);

gint elektron_configure_device_from_file (struct backend *backend, guint8 id,
					  const gchar * filename);

void elektron_destroy_data (struct backend *backend);

gint elektron_set_sample_from_data_sample (struct idata *sample,
					   struct idata *data_sample);
gint elektron_set_data_sample_from_sample (struct idata *data_sample,
					   struct idata *data_footer,
					   struct idata *sample, guint slot);

typedef GByteArray *(*elektron_metadata_transport) (
    struct backend *, GByteArray *, struct controllable *);

gint elektron_get_sample_metadata_with_transport (
    struct backend *, const gchar *, struct sample_metadata *,
    elektron_metadata_transport);

gint elektron_parse_sample_metadata (const GByteArray *info_response,
                                    const GByteArray *hash_response,
                                    struct sample_metadata *metadata);

static void
test_elektron_get_dev_exts ()
{
  const gchar **exts;
  struct backend backend;
  struct elektron_data elektron_data;
  struct fs_operations ops = {
    .name = "fs0"
  };

  backend.data = &elektron_data;

  elektron_data.dev_desc.fs_descs_len = 1;
  snprintf (elektron_data.dev_desc.fs_descs[0].name, LABEL_MAX, "%s", "fs0");
  elektron_data.dev_desc.fs_descs[0].extensions[0] = "ext0";
  elektron_data.dev_desc.fs_descs[0].extensions[1] = NULL;

  printf ("\n");

  exts = elektron_get_dev_exts (&backend, &ops);

  CU_ASSERT_EQUAL (exts, elektron_data.dev_desc.fs_descs[0].extensions);
  CU_ASSERT_EQUAL (strcmp (exts[0], "ext0"), 0);
  CU_ASSERT_EQUAL (exts[1], NULL);
}

static gint
compare_exts (const gchar **a, const gchar **b)
{
  const gchar **x = a;
  const gchar **y = b;

  while (*x)
    {
      if (strcmp (*x, *y))
	{
	  return 1;
	}

      x++;
      y++;
    }

  return *y != NULL;
}

static void
test_elektron_configure_device_from_file ()
{
  const gchar **exts;
  const struct fs_operations *ops;
  struct backend backend;
  struct elektron_data *elektron_data;
  static const gchar *DATA[] = { "data", NULL };
  static const gchar *PRJ[] = { "dtprj", NULL };
  static const gchar *SND[] = { "dtsnd", NULL };

  printf ("\n");

  elektron_data = g_malloc (sizeof (struct elektron_data));
  backend.data = elektron_data;

  elektron_configure_device_from_file (&backend, 12, TEST_DATA_DIR
				       "/../../res/elektron/devices.json");

  CU_ASSERT_EQUAL (elektron_data->dev_desc.fs_descs_len, 7);

  ops = backend_get_fs_operations_by_name (&backend, "sample");

  /* Sample filesystem must expose metadata retrieval. */
  CU_ASSERT_PTR_NOT_NULL (ops->get_sample_metadata);

  /* Invalid arguments must fail without accessing the MIDI device. */
  if (ops->get_sample_metadata)
    {
      struct sample_metadata metadata;

      CU_ASSERT_EQUAL (
          ops->get_sample_metadata (&backend, NULL, &metadata),
          -EINVAL);

      CU_ASSERT_EQUAL (
          ops->get_sample_metadata (&backend, "/test.wav", NULL),
          -EINVAL);
    }
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_NOT_EQUAL (exts, NULL);	//Several sample extensions

  ops = backend_get_fs_operations_by_name (&backend, "data");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DATA), 0);

  ops = backend_get_fs_operations_by_name (&backend, "project");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, PRJ), 0);

  ops = backend_get_fs_operations_by_name (&backend, "sound");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, SND), 0);

  ops = backend_get_fs_operations_by_name (&backend, "ram");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_NOT_EQUAL (exts, NULL);	//Several sample extensions

  ops = backend_get_fs_operations_by_name (&backend, "track");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_NOT_EQUAL (exts, NULL);	//Several sample extensions

  ops = backend_get_fs_operations_by_name (&backend, "track-loop");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_NOT_EQUAL (exts, NULL);	//Several sample extensions

  CU_ASSERT_EQUAL (elektron_data->dev_desc.storage, 3);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);
}

static void
test_elektron_special_exts ()
{
  const gchar **exts;
  const struct fs_operations *ops;
  struct backend backend;
  static const gchar *AHFX_PST[] = { "ahfxpst", "ahpst", NULL };
  static const gchar *DTII_PRJ[] = { "dt2prj", "dtprj", NULL };
  static const gchar *DTII_PST[] = { "dt2pst", "dtsnd", NULL };
  static const gchar *DNII_PRJ[] = { "dn2prj", "dnprj", NULL };
  static const gchar *DNII_PST[] = { "dn2pst", "dnsnd", NULL };

  printf ("\n");

  //Analog Heat +FX

  backend.data = g_malloc (sizeof (struct elektron_data));

  elektron_configure_device_from_file (&backend, 32, TEST_DATA_DIR
				       "/../../res/elektron/devices.json");

  ops = backend_get_fs_operations_by_name (&backend, "preset");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, AHFX_PST), 0);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);

  //Digitakt II

  backend.data = g_malloc (sizeof (struct elektron_data));

  elektron_configure_device_from_file (&backend, 42, TEST_DATA_DIR
				       "/../../res/elektron/devices.json");

  ops = backend_get_fs_operations_by_name (&backend, "project");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DTII_PRJ), 0);

  ops = backend_get_fs_operations_by_name (&backend, "preset-takt-ii");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DTII_PST), 0);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);

  //Digitone II

  backend.data = g_malloc (sizeof (struct elektron_data));

  elektron_configure_device_from_file (&backend, 43, TEST_DATA_DIR
				       "/../../res/elektron/devices.json");

  ops = backend_get_fs_operations_by_name (&backend, "project");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DNII_PRJ), 0);

  ops = backend_get_fs_operations_by_name (&backend, "preset-takt-ii");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DNII_PST), 0);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);
}

static void
test_elektron_data_sample ()
{
  gint err;
  struct idata sample, data_sample, data_footer;
  struct task_control task_control;
  struct sample_info sample_info_src;
  struct sample_load_opts sample_load_opts;
  GByteArray *content_before, *content_after;

  controllable_init (&task_control.controllable);
  task_control.callback = NULL;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);
  err = sample_load_from_file (TEST_DATA_DIR "/connectors/square.wav",
			       &sample, &task_control, &sample_load_opts,
			       &sample_info_src);

  controllable_clear (&task_control.controllable);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  err = elektron_set_data_sample_from_sample (&data_sample, &data_footer,
					      &sample, 3);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      idata_clear (&sample);
      return;
    }

  content_before = idata_steal (&sample);

  g_byte_array_append (data_sample.content, data_footer.content->data,
		       data_footer.content->len);
  err = elektron_set_sample_from_data_sample (&sample, &data_sample);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      idata_clear (&data_sample);
      return;
    }
  idata_clear (&data_sample);
  idata_clear (&data_footer);

  content_after = idata_steal (&sample);

  CU_ASSERT_EQUAL (content_after->len, content_before->len);
  CU_ASSERT_EQUAL (memcmp (content_after->data, content_before->data,
			   content_after->len), 0);

  g_byte_array_free (content_before, TRUE);
  g_byte_array_free (content_after, TRUE);
}


static gint metadata_mock_mode;
static gint metadata_mock_calls;

static GByteArray *
metadata_mock_transport (struct backend *backend, GByteArray *request,
                         struct controllable *controllable)
{
  static const guint8 info_ok[] = {
    0, 0, 0, 0, 0, 1,
    0, 0, 0, 100,
    0x12, 0x34, 0x56, 0x78
  };
  static const guint8 hash_ok[] = {
    0, 0, 0, 0, 0, 1,
    0, 0, 0, 100,
    0xab, 0xcd, 0xef, 0x01
  };
  GByteArray *response;
  guint8 data[sizeof (info_ok)];
  guint8 expected_command;

  (void) backend;
  (void) controllable;

  expected_command = metadata_mock_calls == 0 ? 0x22 : 0x28;

  CU_ASSERT_PTR_NOT_NULL (request);
  if (!request)
    {
      return NULL;
    }

  CU_ASSERT (request->len >= 15);

  if (request->len >= 15)
    {
      CU_ASSERT_EQUAL (request->data[4], expected_command);
      CU_ASSERT_STRING_EQUAL ((const gchar *) &request->data[5],
                              "/test.wav");
    }

  metadata_mock_calls++;
  g_byte_array_free (request, TRUE);

  /* Simulate a timeout on the first or second request. */
  if ((metadata_mock_mode == 6 && metadata_mock_calls == 1) ||
      (metadata_mock_mode == 2 && metadata_mock_calls == 2))
    {
      return NULL;
    }

  memcpy (data, metadata_mock_calls == 1 ? info_ok : hash_ok,
          sizeof (data));

  if (metadata_mock_mode == 1 && metadata_mock_calls == 1)
    data[5] = 0;                 /* Sample not found. */

  if (metadata_mock_mode == 4 && metadata_mock_calls == 2)
    data[9] = 99;                /* Inconsistent size. */

  if (metadata_mock_mode == 5 && metadata_mock_calls == 2)
    data[5] = 0;                 /* Hash request failed. */

  response = g_byte_array_new ();

  if (metadata_mock_mode == 3 && metadata_mock_calls == 1)
    g_byte_array_append (response, data, 5); /* Truncated reply. */
  else
    g_byte_array_append (response, data, sizeof (data));

  return response;
}

static void
test_elektron_sample_metadata_transport ()
{
  struct backend backend = { 0 };
  struct sample_metadata metadata;
  gint err;
  gint mode;

  metadata_mock_mode = 0;
  metadata_mock_calls = 0;

  err = elektron_get_sample_metadata_with_transport (
      &backend, "/test.wav", &metadata, metadata_mock_transport);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (metadata_mock_calls, 2);
  CU_ASSERT_EQUAL (metadata.id, 0x12345678);
  CU_ASSERT_EQUAL (metadata.size, 100);
  CU_ASSERT_EQUAL (metadata.hash, 0xabcdef01);

  for (mode = 1; mode <= 6; mode++)
    {
      metadata_mock_mode = mode;
      metadata_mock_calls = 0;

      metadata.id = 0xdeadbeef;
      metadata.size = 0xdeadbeef;
      metadata.hash = 0xdeadbeef;

      err = elektron_get_sample_metadata_with_transport (
          &backend, "/test.wav", &metadata, metadata_mock_transport);

      CU_ASSERT_EQUAL (err, mode == 1 ? -ENOENT : -EIO);
      CU_ASSERT_EQUAL (metadata_mock_calls,
                       mode == 1 || mode == 3 || mode == 6 ? 1 : 2);

      /* Failed operations must not publish partial metadata. */
      CU_ASSERT_EQUAL (metadata.id, 0xdeadbeef);
      CU_ASSERT_EQUAL (metadata.size, 0xdeadbeef);
      CU_ASSERT_EQUAL (metadata.hash, 0xdeadbeef);
    }
}

static void
test_elektron_sample_metadata ()
{
  /* 0x22: status, size, sample ID. */
  const guint8 info_ok[] = {
    0, 0, 0, 0, 0, 1,
    0, 0, 0, 100,
    0x12, 0x34, 0x56, 0x78
  };

  /* 0x28: status, size, sample hash. */
  const guint8 hash_ok[] = {
    0, 0, 0, 0, 0, 1,
    0, 0, 0, 100,
    0xab, 0xcd, 0xef, 0x01
  };

  GByteArray *info = g_byte_array_new ();
  GByteArray *hash = g_byte_array_new ();
  struct sample_metadata metadata;
  gint err;

  g_byte_array_append (info, info_ok, sizeof (info_ok));
  g_byte_array_append (hash, hash_ok, sizeof (hash_ok));

  /* Valid responses. */
  memset (&metadata, 0, sizeof (metadata));

  err = elektron_parse_sample_metadata (info, hash, &metadata);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (metadata.id, 0x12345678);
  CU_ASSERT_EQUAL (metadata.size, 100);
  CU_ASSERT_EQUAL (metadata.hash, 0xabcdef01);

  /* Sample not found. */
  info->data[5] = 0;
  err = elektron_parse_sample_metadata (info, hash, &metadata);
  CU_ASSERT_EQUAL (err, -ENOENT);
  info->data[5] = 1;

  /* Invalid hash response status. */
  hash->data[5] = 0;
  err = elektron_parse_sample_metadata (info, hash, &metadata);
  CU_ASSERT_EQUAL (err, -EIO);
  hash->data[5] = 1;

  /* Inconsistent sizes. */
  hash->data[9] = 101;
  err = elektron_parse_sample_metadata (info, hash, &metadata);
  CU_ASSERT_EQUAL (err, -EIO);
  hash->data[9] = 100;

  /* Truncated 0x22 response. */
  g_byte_array_set_size (info, 10);
  err = elektron_parse_sample_metadata (info, hash, &metadata);
  CU_ASSERT_EQUAL (err, -EIO);
  g_byte_array_set_size (info, sizeof (info_ok));
  memcpy (info->data, info_ok, sizeof (info_ok));

  /* Truncated 0x28 response. */
  g_byte_array_set_size (hash, 10);
  err = elektron_parse_sample_metadata (info, hash, &metadata);
  CU_ASSERT_EQUAL (err, -EIO);
  g_byte_array_set_size (hash, sizeof (hash_ok));
  memcpy (hash->data, hash_ok, sizeof (hash_ok));

  /* Invalid arguments. */
  err = elektron_parse_sample_metadata (NULL, hash, &metadata);
  CU_ASSERT_EQUAL (err, -EINVAL);

  err = elektron_parse_sample_metadata (info, NULL, &metadata);
  CU_ASSERT_EQUAL (err, -EINVAL);

  err = elektron_parse_sample_metadata (info, hash, NULL);
  CU_ASSERT_EQUAL (err, -EINVAL);

  g_byte_array_free (info, TRUE);
  g_byte_array_free (hash, TRUE);
}

gint
main (gint argc, gchar *argv[])
{
  gint err = 0;

  debug_level = 5;

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("Elektroid elektron tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_sample_metadata_transport",
                    test_elektron_sample_metadata_transport))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_sample_metadata",
                    test_elektron_sample_metadata))
    {
      CU_cleanup_registry ();
      return CU_get_error ();
    }

  if (!CU_add_test (suite, "elektron_get_dev_exts",
		    test_elektron_get_dev_exts))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_configure_device_from_file",
		    test_elektron_configure_device_from_file))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_special_exts",
		    test_elektron_special_exts))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_data_sample", test_elektron_data_sample))
    {
      goto cleanup;
    }

  CU_basic_set_mode (CU_BRM_VERBOSE);

  CU_basic_run_tests ();
  err = CU_get_number_of_tests_failed ();

cleanup:
  CU_cleanup_registry ();
  return err || CU_get_error ();
}
