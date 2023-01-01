#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/scala.h"

#define TEST_MAX_FILE_LEN 1024


static const guint8 SUCCESS_MIDI_MESSAGE[] = {
  0xf0,
  0x7e,
  0x7f,
  8, 6,
  0,				//bank
  1,				//tuning
  0x35, 0x2d, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x20, 0x6a, 0x75, 0x73, 0x74, 0x20, 0x69, 0x6e, 0x74,	//name
  0x40, 0x00,			//pitches
  0x47, 0x41,
  0x42, 0x40,
  0x4a, 0x01,
  0x37, 0x1e,
  0x3e, 0x5f,
  0x39, 0x5f,
  0x41, 0x20,
  0x48, 0x61,
  0x35, 0x7e,
  0x3d, 0x3f,
  0x38, 0x3e,
  0x2e,				//cksum
  0xf7
};

void
test_success ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/success.scl", data, NULL);
  err = scl_init_scala_from_bytes (&scala, data);
  g_byte_array_free (data, TRUE);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_STRING_EQUAL (scala.desc, "5-limit just intonation");
  CU_ASSERT_EQUAL (scala.notes, 12);
  CU_ASSERT_EQUAL (scala.pitches[0], 16 / (double) 15);
  CU_ASSERT_EQUAL (scala.pitches[1], 9 / (double) 8);
  CU_ASSERT_EQUAL (scala.pitches[2], 6 / (double) 5);
  CU_ASSERT_EQUAL (scala.pitches[3], 5 / (double) 4);
  CU_ASSERT_EQUAL (scala.pitches[4], 4 / (double) 3);
  CU_ASSERT_EQUAL (scala.pitches[5], 45 / (double) 32);
  CU_ASSERT_EQUAL (scala.pitches[6], 3 / (double) 2);
  CU_ASSERT_EQUAL (scala.pitches[7], 8 / (double) 5);
  CU_ASSERT_EQUAL (scala.pitches[8], 5 / (double) 3);
  CU_ASSERT_EQUAL (scala.pitches[9], 16 / (double) 9);
  CU_ASSERT_EQUAL (scala.pitches[10], 15 / (double) 8);
  CU_ASSERT_EQUAL (scala.pitches[11], 2 / (double) 1);
}

void
test_success_perfect_5th ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/perfect_5th.scl", data, NULL);
  err = scl_init_scala_from_bytes (&scala, data);
  g_byte_array_free (data, TRUE);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_STRING_EQUAL (scala.desc, "Perfect 5th");
  CU_ASSERT_EQUAL (scala.notes, 1);
  CU_ASSERT_EQUAL (scala.pitches[0], 1.5);
}

void
test_empty_file ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (0);
  err = scl_init_scala_from_bytes (&scala, data);

  CU_ASSERT_EQUAL (err, -EINVAL);

  g_byte_array_free (data, TRUE);
}

void
test_too_many_notes ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/too_many_notes.scl", data, NULL);
  err = scl_init_scala_from_bytes (&scala, data);

  CU_ASSERT_EQUAL (err, -ERANGE);

  g_byte_array_free (data, TRUE);
}

void
test_no_notes ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/no_notes.scl", data, NULL);
  err = scl_init_scala_from_bytes (&scala, data);

  CU_ASSERT_EQUAL (err, -ERANGE);

  g_byte_array_free (data, TRUE);
}

void
test_unmatching_notes ()
{
  struct scala scala;
  GByteArray *data;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/unmatching_notes.scl", data, NULL);
  err = scl_init_scala_from_bytes (&scala, data);

  CU_ASSERT_EQUAL (err, -EINVAL);

  g_byte_array_free (data, TRUE);
}

void
test_get_midi_message ()
{
  struct scala scala;
  GByteArray *data, *msg;
  gint err;

  printf ("\n");

  data = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  load_file ("res/scala/success.scl", data, NULL);
  msg = scl_get_2_byte_tuning_msg_from_scala_file (data, 0, 1);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (msg->len, sizeof (SUCCESS_MIDI_MESSAGE));
  CU_ASSERT_EQUAL (memcmp
		   (msg->data, SUCCESS_MIDI_MESSAGE,
		    sizeof (SUCCESS_MIDI_MESSAGE)), 0);

  g_byte_array_free (data, TRUE);
  g_byte_array_free (msg, TRUE);
}

int
main (int argc, char *argv[])
{
  int err;

  debug_level = 5;

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("Elektroid scala tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_success", test_success))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "success_perfect_5th", test_success_perfect_5th))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_empty_file", test_empty_file))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_too_many_notes", test_too_many_notes))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "no_notes", test_no_notes))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "unmatching_notes", test_unmatching_notes))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_get_midi_message", test_get_midi_message))
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
