#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/microfreak.h"

#define HEADER_PAYLOAD "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x10\x4e\x65\x72\x76\x6f\x75\x73\x4b\x65\x79\x73\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

gint microfreak_serialize_preset (GByteArray * output,
				  struct microfreak_preset *mfp);

gint microfreak_deserialize_preset (struct microfreak_preset *mfp,
				    GByteArray * input);

gint microfreak_serialize_wavetable (struct idata *serialized,
				     struct idata *wavetable);

gint microfreak_deserialize_wavetable (struct idata *wavetable,
				       struct idata *serialized);

void microfreak_midi_msg_to_8bit_msg (guint8 *, guint8 *);

void microfreak_8bit_msg_to_midi_msg (guint8 *, guint8 *);

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
  struct idata src, dst, serialized;
  gchar *text;
  GByteArray *src_content;

  printf ("\n");

  src_content = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE);
  src_content->len = MICROFREAK_WAVETABLE_SIZE;
  idata_init (&src, src_content, strdup ("0123456789abcde"), NULL);
  v = (gint8 *) src.content->data;
  for (guint i = 0; i < MICROFREAK_WAVETABLE_SIZE; i++, v++)
    {
      *v = (g_random_int () & 0xff) - 128;
    }

  text = debug_get_hex_data (debug_level, src.content->data,
			     src.content->len);
  debug_print (0, "src (%d): %s\n", src.content->len, text);
  g_free (text);

  err = microfreak_serialize_wavetable (&serialized, &src);
  CU_ASSERT_EQUAL (err, 0);

  debug_print (0, "src (%d): %.*s\n", serialized.content->len,
	       serialized.content->len, serialized.content->data);

  err = microfreak_deserialize_wavetable (&dst, &serialized);
  CU_ASSERT_EQUAL (err, 0);
  CU_ASSERT_EQUAL (dst.content->len, MICROFREAK_WAVETABLE_SIZE);
  CU_ASSERT_EQUAL (strcmp (dst.name, "0123456789abcde"), 0);

  text = debug_get_hex_data (debug_level, dst.content->data,
			     dst.content->len);
  debug_print (0, "dst (%d): %s\n", dst.content->len, text);
  g_free (text);

  err = memcmp (src.content->data, dst.content->data,
		MICROFREAK_WAVETABLE_SIZE);
  CU_ASSERT_EQUAL (err, 0);

  idata_free (&src);
  idata_free (&dst);
  idata_free (&serialized);
}

void
test_bad_deserialization_wavetable ()
{
  gint err;
  struct idata dst, serialized;
  GByteArray *serialized_content;

  printf ("\n");

  serialized_content = g_byte_array_new ();
  idata_init (&serialized, serialized_content, NULL, NULL);

  err = microfreak_deserialize_wavetable (&dst, &serialized);
  CU_ASSERT_NOT_EQUAL (err, 0);

  idata_free (&serialized);
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

gint
main (gint argc, gchar *argv[])
{
  gint err = 0;

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
