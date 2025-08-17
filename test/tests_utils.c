#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"

void
test_filename_matches_exts ()
{
  const gchar *exts0[] = { NULL };
  const gchar *exts1[] = { "ext1", NULL };
  const gchar *exts2[] = { "ext1", "ext2", NULL };

  printf ("\n");

  CU_ASSERT_EQUAL (filename_matches_exts ("file.ext1", NULL), TRUE);

  CU_ASSERT_EQUAL (filename_matches_exts ("file.ext1", exts0), FALSE);

  CU_ASSERT_EQUAL (filename_matches_exts ("file", exts1), FALSE);
  CU_ASSERT_EQUAL (filename_matches_exts ("file.ext1", exts1), TRUE);

  CU_ASSERT_EQUAL (filename_matches_exts ("file.ext2", exts2), TRUE);
  CU_ASSERT_EQUAL (filename_matches_exts ("file.eXt2", exts2), TRUE);
}

void
test_filename_get_ext ()
{
  printf ("\n");

  CU_ASSERT_STRING_EQUAL ("", filename_get_ext ("file"));
  CU_ASSERT_STRING_EQUAL ("", filename_get_ext ("file."));
  CU_ASSERT_STRING_EQUAL ("ext1", filename_get_ext ("file.ext2.ext1"));
}

void
test_token_is_in_text ()
{
  printf ("\n");

  CU_ASSERT_FALSE (token_is_in_text ("foo", "drum_loop"));

  CU_ASSERT_TRUE (token_is_in_text ("drum", "drum_loop"));
  CU_ASSERT_TRUE (token_is_in_text ("loop", "drum loop"));
  CU_ASSERT_TRUE (token_is_in_text ("loop", "drumloop"));

  CU_ASSERT_TRUE (token_is_in_text ("drum", "DRUM_LOOP"));
  CU_ASSERT_TRUE (token_is_in_text ("loop", "DRUM LOOP"));
  CU_ASSERT_TRUE (token_is_in_text ("loop", "DRUMLOOP"));

  CU_ASSERT_TRUE (token_is_in_text ("drum", "drüm"));
  CU_ASSERT_TRUE (token_is_in_text ("drum", "DRÜM"));
}

void
test_command_set_parts ()
{
  gchar *conn, *fs, *op;

  printf ("\n");

  CU_ASSERT_EQUAL (command_set_parts ("a", &conn, &fs, &op), -EINVAL);

  CU_ASSERT_EQUAL (command_set_parts ("a-b", &conn, &fs, &op), -EINVAL);

  CU_ASSERT_EQUAL (command_set_parts ("a-b-c", &conn, &fs, &op), 0);
  CU_ASSERT_STRING_EQUAL (conn, "a");
  CU_ASSERT_STRING_EQUAL (fs, "b");
  CU_ASSERT_STRING_EQUAL (op, "c");

  //This test is just here to document the issue with the hyphen as the parts separator.
  //No workaround is possible as both the connector and the filesystem might use a hyphen.
  CU_ASSERT_EQUAL (command_set_parts ("a-b-c-d", &conn, &fs, &op), 0);
  CU_ASSERT_STRING_EQUAL (conn, "a");
  CU_ASSERT_STRING_EQUAL (fs, "b");
  CU_ASSERT_STRING_EQUAL (op, "c-d");

  CU_ASSERT_EQUAL (command_set_parts ("a-a:b:c", &conn, &fs, &op), 0);
  CU_ASSERT_STRING_EQUAL (conn, "a-a");
  CU_ASSERT_STRING_EQUAL (fs, "b");
  CU_ASSERT_STRING_EQUAL (op, "c");
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

  if (!CU_add_test (suite, "filename_matches_exts",
		    test_filename_matches_exts))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "filename_get_ext", test_filename_get_ext))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "token_is_in_text", test_token_is_in_text))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "command_set_parts", test_command_set_parts))
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
