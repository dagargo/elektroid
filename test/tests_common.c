#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"

void
test_success ()
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

int
main (int argc, char *argv[])
{
  int err = 0;

  debug_level = 5;

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("common connector tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_success", test_success))
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
