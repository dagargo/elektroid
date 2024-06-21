#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"

void
test_common_remove_slot_name_from_path ()
{
  gchar *str;

  printf ("\n");

  str = strdup ("/a/b:asdf");
  common_remove_slot_name_from_path (str);
  CU_ASSERT_STRING_EQUAL (str, "/a/b");
  g_free (str);

  str = strdup ("/a/b");
  common_remove_slot_name_from_path (str);
  CU_ASSERT_STRING_EQUAL (str, "/a/b");
  g_free (str);

  str = strdup ("/a/");
  common_remove_slot_name_from_path (str);
  CU_ASSERT_STRING_EQUAL (str, "/a/");
  g_free (str);

  str = strdup ("/");
  common_remove_slot_name_from_path (str);
  CU_ASSERT_STRING_EQUAL (str, "/");
  g_free (str);

  str = strdup ("");
  common_remove_slot_name_from_path (str);
  CU_ASSERT_STRING_EQUAL (str, "");
  g_free (str);
}

void
test_common_slot_get_id_name_from_path ()
{
  gchar *str;
  guint id;
  gint err;

  printf ("\n");

  err = common_slot_get_id_name_from_path ("", &id, &str);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_name_from_path ("/", &id, &str);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_name_from_path ("/1", &id, NULL);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);
  g_free (str);

  err = common_slot_get_id_name_from_path ("/p/1", &id, NULL);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);
  g_free (str);

  err = common_slot_get_id_name_from_path ("/p/1", &id, &str);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_name_from_path ("/1:", &id, &str);
  CU_ASSERT_TRUE (err == -EINVAL);

  err = common_slot_get_id_name_from_path ("/:a", &id, &str);
  CU_ASSERT_TRUE (id == -EINVAL);

  err = common_slot_get_id_name_from_path ("/p/1:a", &id, NULL);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);

  err = common_slot_get_id_name_from_path ("/p/1:a", &id, &str);
  CU_ASSERT_TRUE (err == 0);
  CU_ASSERT_TRUE (id == 1);
  CU_ASSERT_STRING_EQUAL (str, "a");
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

  if (!CU_add_test (suite, "common_remove_slot_name_from_path",
		    test_common_remove_slot_name_from_path))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_slot_get_id_name_from_path",
		    test_common_remove_slot_name_from_path))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_get_sanitized_name",
		    test_common_get_sanitized_name))
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
