#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"

void
test_common_slot_get_id_from_path ()
{
  guint id;
  gint err;

  printf ("\n");

  err = common_slot_get_id_from_path ("", &id);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_from_path ("/", &id);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_from_path ("/1", &id);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);

  err = common_slot_get_id_from_path ("/p/1", &id);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);
}

void
test_common_get_sanitized_name ()
{
  gchar *str;

  printf ("\n");

  str = common_get_sanitized_name ("asdf", NULL, '?');
  CU_ASSERT_STRING_EQUAL (str, "asdf");
  g_free (str);

  str = common_get_sanitized_name ("ásdf", NULL, '?');
  CU_ASSERT_STRING_EQUAL (str, "asdf");
  g_free (str);

  str = common_get_sanitized_name ("asdf", "asd", '?');
  CU_ASSERT_STRING_EQUAL (str, "asd?");
  g_free (str);

  str = common_get_sanitized_name ("ásdf", "asd", '?');
  CU_ASSERT_STRING_EQUAL (str, "asd?");
  g_free (str);

  str = common_get_sanitized_name ("ásdf", "asd", 0);
  CU_ASSERT_STRING_EQUAL (str, "asd");
  g_free (str);
}

void
test_common_slot_get_download_path ()
{
  struct backend backend;
  struct fs_operations ops;
  gchar *path;
  struct idata idata;

  snprintf (backend.name, LABEL_MAX, "Dev Name");
  ops.name = "fsname";
  ops.ext = "ext";

  idata_init (&idata, NULL, strdup ("name"), NULL);

  path = common_slot_get_download_path_n (&backend, &ops, "/dst_dir", "/1",
					  &idata);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname 1 - name.ext", path);
  g_free (path);

  path = common_slot_get_download_path_nn (&backend, &ops, "/dst_dir", "/1",
					   &idata);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname 01 - name.ext", path);
  g_free (path);

  path = common_slot_get_download_path_nnn (&backend, &ops, "/dst_dir", "/1",
					    &idata);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname 001 - name.ext", path);
  g_free (path);

  path = common_slot_get_download_path (&backend, &ops, "/dst_dir", "/1",
					&idata, 0);
  printf ("%s\n", path);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname - name.ext", path);
  g_free (path);

  idata_free (&idata);

  idata_init (&idata, NULL, NULL, NULL);

  path = common_slot_get_download_path_n (&backend, &ops, "/dst_dir", "/1",
					  &idata);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname 1.ext", path);
  g_free (path);

  idata_free (&idata);
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
  CU_pSuite suite = CU_add_suite ("Elektroid common connector tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_slot_get_id_from_path",
		    test_common_slot_get_id_from_path))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_get_sanitized_name",
		    test_common_get_sanitized_name))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_slot_get_download_path",
		    test_common_slot_get_download_path))
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
