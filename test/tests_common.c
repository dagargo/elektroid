#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"

static void
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

static void
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
test_common_to_os_sanitized_name ()
{
  gchar *str = strdup ("\\^/");

  printf ("\n");

  common_to_os_sanitized_name (str);
  CU_ASSERT_STRING_EQUAL (str, "?^?");
  g_free (str);
}

static const gchar *EXTS[] = { "ext", NULL };

static const gchar **
get_exts (struct backend *backend, const struct fs_operations *ops)
{
  return EXTS;
}

static void
test_common_slot_get_download_path ()
{
  struct backend backend;
  struct fs_operations ops;
  gchar *path;
  struct idata idata;

  snprintf (backend.name, LABEL_MAX, "Dev Name");
  ops.name = "fsname";
  ops.get_exts = get_exts;

  idata_init (&idata, NULL, strdup ("name"), NULL, NULL);

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

  idata_init (&idata, NULL, NULL, NULL, NULL);

  path = common_slot_get_download_path_n (&backend, &ops, "/dst_dir", "/1",
					  &idata);
  CU_ASSERT_STRING_EQUAL ("/dst_dir/Dev Name fsname 1.ext", path);
  g_free (path);

  idata_free (&idata);
}

static void
test_8bit_conversions ()
{
  guint8 *v;
  guint8 src[119], dst[136], bak[119];

  printf ("\n");

  v = src;
  for (gint i = 0; i < 32; i++, v++)
    {
      *v = g_random_int () & 0xff;
    }
  src[0] |= 0x80;		//Ensures MSB at 1.
  src[31] |= 0x80;		//Ensures MSB at 1.

  common_8bit_msg_to_midi_msg (src, dst, 32);
  common_midi_msg_to_8bit_msg (dst, bak, 37);
  CU_ASSERT_EQUAL (memcmp (src, bak, 32), 0);

  v = src;
  for (gint i = 0; i < 119; i++, v++)
    {
      *v = g_random_int () & 0xff;
    }
  src[0] |= 0x80;		//Ensures MSB at 1.
  src[118] |= 0x80;		//Ensures MSB at 1.

  common_8bit_msg_to_midi_msg (src, dst, 119);
  common_midi_msg_to_8bit_msg (dst, bak, 136);
  CU_ASSERT_EQUAL (memcmp (src, bak, 119), 0);
}

static void
test_common_8bit_msg_to_midi_msg_size ()
{
  CU_ASSERT_EQUAL (common_8bit_msg_to_midi_msg_size (0), 0);
  CU_ASSERT_EQUAL (common_8bit_msg_to_midi_msg_size (32), 37);
  CU_ASSERT_EQUAL (common_8bit_msg_to_midi_msg_size (119), 136);
  CU_ASSERT_EQUAL (common_8bit_msg_to_midi_msg_size (14420), 16480);
  CU_ASSERT_EQUAL (common_8bit_msg_to_midi_msg_size (917494), 1048565);
}

static void
test_common_midi_msg_to_8bit_msg_size ()
{
  CU_ASSERT_EQUAL (common_midi_msg_to_8bit_msg_size (0), 0);
  CU_ASSERT_EQUAL (common_midi_msg_to_8bit_msg_size (37), 32);
  CU_ASSERT_EQUAL (common_midi_msg_to_8bit_msg_size (136), 119);
  CU_ASSERT_EQUAL (common_midi_msg_to_8bit_msg_size (16480), 14420);
  CU_ASSERT_EQUAL (common_midi_msg_to_8bit_msg_size (1048565), 917494);
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

  if (!CU_add_test (suite, "test_common_to_os_sanitized_name",
		    test_common_to_os_sanitized_name))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "common_slot_get_download_path",
		    test_common_slot_get_download_path))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "8bit_conversions", test_8bit_conversions))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "common_8bit_msg_to_midi_msg_size",
       test_common_8bit_msg_to_midi_msg_size))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "common_midi_msg_to_8bit_msg_size",
       test_common_midi_msg_to_8bit_msg_size))
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
