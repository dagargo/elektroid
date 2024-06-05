#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/microfreak.h"

#define HEADER_PAYLOAD "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x10\x4e\x65\x72\x76\x6f\x75\x73\x4b\x65\x79\x73\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

void
test_serialization_deserialization_preset ()
{
  struct microfreak_preset mfp_src;
  struct microfreak_preset mfp_dst;
  gint err;
  GByteArray *serialized = g_byte_array_sized_new (64 * 1024);

  printf ("\n");

  memcpy (mfp_src.header, HEADER_PAYLOAD, MICROFREAK_PRESET_HEADER_MSG_LEN);
  mfp_src.parts = MICROFREAK_PRESET_PARTS;
  for (guint i = 0; i < MICROFREAK_PRESET_DATALEN; i++)
    {
      mfp_src.data[i] = g_random_int () & 0x7f;
    }

  err = microfreak_serialize_preset (serialized, &mfp_src);
  CU_ASSERT_EQUAL (err, 0);

  err = microfreak_deserialize_preset (&mfp_dst, serialized);
  CU_ASSERT_EQUAL (err, 0);

  err = memcmp (mfp_src.header, mfp_dst.header,
		MICROFREAK_PRESET_HEADER_MSG_LEN);
  CU_ASSERT_EQUAL (err, 0);

  CU_ASSERT_EQUAL (mfp_dst.parts, mfp_src.parts);

  err = memcmp (mfp_src.data, mfp_dst.data, MICROFREAK_PRESET_DATALEN);
  CU_ASSERT_EQUAL (err, 0);
}

void
test_serialization_deserialization_preset_init ()
{
  struct microfreak_preset mfp_src;
  struct microfreak_preset mfp_dst;
  gint err;
  GByteArray *serialized = g_byte_array_sized_new (64 * 1024);

  printf ("\n");

  memcpy (mfp_src.header, HEADER_PAYLOAD, MICROFREAK_PRESET_HEADER_MSG_LEN);
  mfp_src.header[3] = 0x08;	//Init
  mfp_src.parts = 0;

  err = microfreak_serialize_preset (serialized, &mfp_src);
  CU_ASSERT_EQUAL (err, 0);

  err = microfreak_deserialize_preset (&mfp_dst, serialized);
  CU_ASSERT_EQUAL (err, 0);

  err = memcmp (mfp_src.header, mfp_dst.header,
		MICROFREAK_PRESET_HEADER_MSG_LEN);
  CU_ASSERT_EQUAL (err, 0);

  CU_ASSERT_EQUAL (mfp_dst.parts, mfp_src.parts);
}

void
test_serialization_deserialization_wavetable ()
{
  gint err;
  gint8 *v;
  GByteArray *src = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);
  GByteArray *dst = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);
  GByteArray *serialized = g_byte_array_new ();
  gchar *text;

  printf ("\n");

  v = (gint8 *) src->data;
  src->len = MICROFREAK_WAVETABLE_SIZE;
  for (guint i = 0; i < MICROFREAK_WAVETABLE_SIZE; i++, v++)
    {
      *v = (g_random_int () & 0xff) - 128;
    }

  err = microfreak_serialize_wavetable (serialized, src);
  CU_ASSERT_EQUAL (err, 0);

  err = microfreak_deserialize_wavetable (dst, serialized);
  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (dst->len, MICROFREAK_WAVETABLE_SIZE);

  text = debug_get_hex_data (debug_level, src->data, src->len);
  debug_print (0, "src (%d): %s\n", src->len, text);
  g_free (text);

  text = debug_get_hex_data (debug_level, dst->data, dst->len);
  debug_print (0, "dst (%d): %s\n", dst->len, text);
  g_free (text);

  err = memcmp (src->data, dst->data, MICROFREAK_WAVETABLE_SIZE);
  CU_ASSERT_EQUAL (err, 0);

  g_byte_array_free (src, TRUE);
  g_byte_array_free (dst, TRUE);
  g_byte_array_free (serialized, TRUE);
}

void
test_8bit_header_conversions ()
{
  struct microfreak_sample_header src, dst;
  guint8 msg_midi[MICROFREAK_WAVE_MSG_SIZE];
  gchar *text;
  guint8 *v = (guint8 *) & src;

  printf ("\n");

  for (gint i = 0; i < sizeof (src); i++, v++)
    {
      *v = g_random_int () & 0xff;
    }
  snprintf (src.name, MICROFREAK_SAMPLE_NAME_LEN, "%s", "foo");

  text = debug_get_hex_data (debug_level, (guint8 *) & src, sizeof (src));
  debug_print (0, "src (%zd): %s\n", sizeof (src), text);
  g_free (text);

  microfreak_8bit_msg_to_midi_msg ((guint8 *) & src, msg_midi);

  text = debug_get_hex_data (debug_level, msg_midi, MICROFREAK_WAVE_MSG_SIZE);
  debug_print (0, "msg (%zd): %s\n", MICROFREAK_WAVE_MSG_SIZE, text);
  g_free (text);

  microfreak_midi_msg_to_8bit_msg (msg_midi, (guint8 *) & dst);

  text = debug_get_hex_data (debug_level, (guint8 *) & dst, sizeof (dst));
  debug_print (0, "dst (%zd): %s\n", sizeof (dst), text);
  g_free (text);

  CU_ASSERT_EQUAL (memcmp (&src, &dst, sizeof (src)), 0);
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
  CU_pSuite suite = CU_add_suite ("Elektroid microfreak tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_serialization_deserialization_preset",
		    test_serialization_deserialization_preset))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_serialization_deserialization_preset_init",
		    test_serialization_deserialization_preset_init))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_serialization_deserialization_wavetable",
		    test_serialization_deserialization_wavetable))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_sample_header_conversions",
		    test_8bit_header_conversions))
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
