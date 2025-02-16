#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"

void
test_file_matches_extensions ()
{
  const gchar *exts0[] = { NULL };
  const gchar *exts1[] = { "ext1", NULL };
  const gchar *exts2[] = { "ext1", "ext2", NULL };

  printf ("\n");

  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext1", NULL), TRUE);

  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext1", exts0), FALSE);

  CU_ASSERT_EQUAL (file_matches_extensions ("file", exts1), FALSE);
  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext1", exts1), TRUE);

  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext2", exts2), TRUE);
  CU_ASSERT_EQUAL (file_matches_extensions ("file.eXt2", exts2), TRUE);
}

void
test_filename_get_ext ()
{
  printf ("\n");

  CU_ASSERT_STRING_EQUAL ("", filename_get_ext ("file"));
  CU_ASSERT_STRING_EQUAL ("", filename_get_ext ("file."));
  CU_ASSERT_STRING_EQUAL ("ext1", filename_get_ext ("file.ext2.ext1"));
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
  CU_pSuite suite = CU_add_suite ("Elektroid utils tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "file_matches_extensions",
		    test_file_matches_extensions))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "filename_get_ext", test_filename_get_ext))
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
