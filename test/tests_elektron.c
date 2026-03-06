#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../config.h"
#include "../src/sample.h"
#include "../src/utils.h"
#include "../src/connectors/package.h"
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
					   struct idata *sample, guint slot);

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

  elektron_data.device_desc.fs_descs_len = 1;
  snprintf (elektron_data.device_desc.fs_descs[0].name, LABEL_MAX,
	    "%s", "fs0");
  elektron_data.device_desc.fs_descs[0].extensions[0] = "ext0";
  elektron_data.device_desc.fs_descs[0].extensions[1] = NULL;

  printf ("\n");

  exts = elektron_get_dev_exts (&backend, &ops);

  CU_ASSERT_EQUAL (exts, elektron_data.device_desc.fs_descs[0].extensions);
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

  CU_ASSERT_EQUAL (elektron_data->device_desc.fs_descs_len, 7);

  ops = backend_get_fs_operations_by_name (&backend, "sample");
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

  CU_ASSERT_EQUAL (elektron_data->device_desc.storage, 3);

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
  struct idata sample, data_sample;
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

  err = elektron_set_data_sample_from_sample (&data_sample, &sample, 3);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      idata_clear (&sample);
      return;
    }

  content_before = idata_steal (&sample);

  err = elektron_set_sample_from_data_sample (&sample, &data_sample);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      idata_clear (&data_sample);
      return;
    }

  idata_clear (&data_sample);

  content_after = idata_steal (&sample);

  CU_ASSERT_EQUAL (content_after->len, content_before->len);
  CU_ASSERT_EQUAL (memcmp (content_after->data, content_before->data,
			   content_after->len), 0);

  g_byte_array_free (content_before, TRUE);
  g_byte_array_free (content_after, TRUE);
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
