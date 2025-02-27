#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../config.h"
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

void
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

void
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

  CU_ASSERT_EQUAL (elektron_data->device_desc.fs_descs_len, 4);

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

  CU_ASSERT_EQUAL (elektron_data->device_desc.storage, 3);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);
}

void
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

  ops = backend_get_fs_operations_by_name (&backend, "preset");
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

  ops = backend_get_fs_operations_by_name (&backend, "preset");
  exts = ops->get_exts (&backend, ops);
  CU_ASSERT_EQUAL (compare_exts (exts, DNII_PST), 0);

  elektron_destroy_data (&backend);
  CU_ASSERT_EQUAL (backend.data, NULL);
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

  CU_basic_set_mode (CU_BRM_VERBOSE);

  CU_basic_run_tests ();
  err = CU_get_number_of_tests_failed ();

cleanup:
  CU_cleanup_registry ();
  return err || CU_get_error ();
}
