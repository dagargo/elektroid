#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/scala.h"

#define TEST_MAX_FILE_LEN 1024

static const guint8 SUCCESS_OCTAVE_MIDI_MSG[] = {
  0xf0,
  0x7e,
  0x7f,
  8, 6,
  0,				//bank
  0,				//tuning
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
  0x2f,				//cksum
  0xf7
};

static const guint8 SUCCESS_BULK_MIDI_MSG_HEADER[] = {
  0xf0,
  0x7e,
  0x7f,
  8, 1,
  0,				//tuning
  0x35, 0x2d, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x20, 0x6a, 0x75, 0x73, 0x74, 0x20, 0x69, 0x6e, 0x74	//name
};

static const guint8 SUCCESS_BULK_MIDI_MSG_OCTAVE_DATA[] = {
  0x00, 0x00, 0x00,
  0x01, 0x0f, 0x03,
  0x02, 0x05, 0x00,
  0x03, 0x14, 0x04,
  0x03, 0x6e, 0x45,
  0x04, 0x7d, 0x48,
  0x05, 0x73, 0x46,
  0x07, 0x02, 0x40,
  0x08, 0x11, 0x43,
  0x08, 0x6c, 0x05,
  0x09, 0x7b, 0x08,
  0x0a, 0x71, 0x06
};

static const guint8 SUCCESS_OCTAVE_MIDI_MSG_TET[] = {
  0xf0,
  0x7e,
  0x7f,
  8, 6,
  0,				//bank
  0,				//tuning
  0x54, 0x45, 0x54, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,	//name
  0x40, 0x00,			//pitches
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x40, 0x00,
  0x6a,				//cksum
  0xf7
};

static const guint8 SUCCESS_BULK_MIDI_MSG_HEADER_TET[] = {
  0xf0,
  0x7e,
  0x7f,
  8, 1,
  0,				//tuning
  0x54, 0x45, 0x54, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20	//name
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
test_get_2_byte_octave_midi_message ()
{
  struct scala scala;
  GByteArray *msg;
  gint err;

  printf ("\n");

  msg = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  err =
    scl_get_2_byte_octave_tuning_msg_from_scala_file ("res/scala/success.scl",
						      msg, NULL);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (msg->len, sizeof (SUCCESS_OCTAVE_MIDI_MSG));
  CU_ASSERT_EQUAL (memcmp
		   (msg->data, SUCCESS_OCTAVE_MIDI_MSG,
		    sizeof (SUCCESS_OCTAVE_MIDI_MSG)), 0);

  g_byte_array_free (msg, TRUE);
}

void
test_get_bulk_tuning_midi_message ()
{
  gint err;
  struct scala scala;
  GByteArray *msg;
  guint8 *b, *f_data;

  printf ("\n");

  msg = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  err = scl_get_key_based_tuning_msg_from_scala_file ("res/scala/success.scl",
						      msg, NULL);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (msg->len, 408);

  CU_ASSERT_EQUAL (memcmp
		   (msg->data, SUCCESS_BULK_MIDI_MSG_HEADER,
		    sizeof (SUCCESS_BULK_MIDI_MSG_HEADER)), 0);

  //Test the first octave.
  f_data = msg->data + sizeof (SUCCESS_BULK_MIDI_MSG_HEADER);
  CU_ASSERT_EQUAL (memcmp
		   (f_data, SUCCESS_BULK_MIDI_MSG_OCTAVE_DATA,
		    sizeof (SUCCESS_BULK_MIDI_MSG_OCTAVE_DATA)), 0);

  //Test the nots above the first octave by comparing them to the notes in the first one.
  b = f_data + 12 * 3;
  for (guint8 i = 12; i < SCALA_MIDI_NOTES; i++, b += 3)
    {
      gint offset = (i % 12) * 3;
      gint octave = (i / 12) * 12;
      CU_ASSERT_EQUAL (*b, *(f_data + offset) + octave);
      CU_ASSERT_EQUAL (*(b + 1), *(f_data + offset + 1));
      CU_ASSERT_EQUAL (*(b + 2), *(f_data + offset + 2));
    }

  CU_ASSERT_EQUAL (msg->data[406], 0x03);
  CU_ASSERT_EQUAL (msg->data[407], 0xf7);

  g_byte_array_free (msg, TRUE);
}

void
test_get_2_byte_octave_midi_message_tet ()
{
  struct scala scala;
  GByteArray *msg;
  gint err;

  printf ("\n");

  msg = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  err =
    scl_get_2_byte_octave_tuning_msg_from_scala_file ("res/scala/TET.scl",
						      msg, NULL);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (msg->len, sizeof (SUCCESS_OCTAVE_MIDI_MSG_TET));
  CU_ASSERT_EQUAL (memcmp
		   (msg->data, SUCCESS_OCTAVE_MIDI_MSG_TET,
		    sizeof (SUCCESS_OCTAVE_MIDI_MSG_TET)), 0);

  g_byte_array_free (msg, TRUE);
}

void
test_get_bulk_tuning_midi_message_tet ()
{
  gint err;
  guint8 *b;
  struct scala scala;
  GByteArray *msg;

  printf ("\n");

  msg = g_byte_array_sized_new (TEST_MAX_FILE_LEN);
  err = scl_get_key_based_tuning_msg_from_scala_file ("res/scala/TET.scl",
						      msg, NULL);

  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (msg->len, 408);

  CU_ASSERT_EQUAL (memcmp
		   (msg->data, SUCCESS_BULK_MIDI_MSG_HEADER_TET,
		    sizeof (SUCCESS_BULK_MIDI_MSG_HEADER_TET)), 0);

  b = msg->data + sizeof (SUCCESS_BULK_MIDI_MSG_HEADER_TET);
  for (guint8 i = 0; i < SCALA_MIDI_NOTES; i++, b += 3)
    {
      CU_ASSERT_EQUAL (*b, i);
      CU_ASSERT_EQUAL (*(b + 1), 0);
      CU_ASSERT_EQUAL (*(b + 2), 0);
    }

  CU_ASSERT_EQUAL (msg->data[406], 0x6d);
  CU_ASSERT_EQUAL (msg->data[407], 0xf7);

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

  if (!CU_add_test
      (suite, "test_get_2_byte_octave_midi_message",
       test_get_2_byte_octave_midi_message))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "test_get_bulk_tuning_midi_message",
       test_get_bulk_tuning_midi_message))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "test_get_2_byte_octave_midi_message_tet",
       test_get_2_byte_octave_midi_message_tet))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "test_get_bulk_tuning_midi_message_tet",
       test_get_bulk_tuning_midi_message_tet))
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
