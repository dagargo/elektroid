#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/connector.h"

void
test_item_iterator_is_dir_or_matches_exts ()
{
  const gchar *exts0[] = { NULL };
  const gchar *exts1[] = { "ext1", NULL };
  const gchar *exts2[] = { "ext2", NULL };

  struct item_iterator iter;

  printf ("\n");

  iter.item.type = ITEM_TYPE_DIR;
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_exts (&iter, NULL), TRUE);
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_exts (&iter, exts0), TRUE);

  iter.item.type = ITEM_TYPE_FILE;
  snprintf (iter.item.name, LABEL_MAX, "%s", "file.ext1");
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_exts (&iter, exts1), TRUE);
  CU_ASSERT_EQUAL (item_iterator_is_dir_or_matches_exts (&iter, exts2),
		   FALSE);
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
  CU_pSuite suite = CU_add_suite ("Elektroid connector tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "item_iterator_is_dir_or_matches_exts",
		    test_item_iterator_is_dir_or_matches_exts))
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
