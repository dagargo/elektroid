#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"

void
test_file_matches_extensions ()
{
  GSList *exts = NULL;

  printf ("\n");

  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext1", exts), TRUE);

  exts = g_slist_append (exts, "ext1");
  CU_ASSERT_EQUAL (file_matches_extensions ("file", exts), FALSE);
  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext1", exts), TRUE);

  exts = g_slist_append (exts, "ext2");
  CU_ASSERT_EQUAL (file_matches_extensions ("file.ext2", exts), TRUE);
  CU_ASSERT_EQUAL (file_matches_extensions ("file.eXt2", exts), TRUE);

  g_slist_free (exts);		//Extensions in this test are not duplicated
}

void
test_item_iterator_is_dir_or_matches_extensions ()
{
  GSList *exts = NULL;
  struct item_iterator iter;

  printf ("\n");

  iter.item.type = ELEKTROID_DIR;
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_extensions (&iter, exts),
		   TRUE);

  iter.item.type = ELEKTROID_FILE;
  snprintf (iter.item.name, LABEL_MAX, "%s", "file.ext1");
  exts = g_slist_append (exts, "ext2");
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_extensions (&iter, exts),
		   FALSE);

  g_slist_free (exts);		//Extensions in this test are not duplicated
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

  if (!CU_add_test (suite, "item_iterator_is_dir_or_matches_extensions",
		    test_item_iterator_is_dir_or_matches_extensions))
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
