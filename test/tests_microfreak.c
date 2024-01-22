#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/microfreak.h"

#define HEADER_PAYLOAD "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x10\x4e\x65\x72\x76\x6f\x75\x73\x4b\x65\x79\x73\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

void
test_serialization_deserialization ()
{
  struct microfreak_preset mfp_src;
  struct microfreak_preset mfp_dst;
  gint err;
  GByteArray *serialized = g_byte_array_sized_new (64 * 1024);

  printf ("\n");

  memcpy (mfp_src.header, HEADER_PAYLOAD, MICROFREAK_PRESET_HEADER_MSG_LEN);
  mfp_src.parts = MICROFREAK_PRESET_PARTS;
  for (gint i = 0; i < mfp_src.parts; i++)
    {
      for (gint j = 0; j < MICROFREAK_PRESET_PART_LEN; j++)
	{
	  mfp_src.part[i][j] = g_random_int () & 0x7f;
	}
    }

  err = microfreak_serialize_preset (serialized, &mfp_src);
  CU_ASSERT_EQUAL (err, 0);

  err = microfreak_deserialize_preset (&mfp_dst, serialized);
  CU_ASSERT_EQUAL (err, 0);

  err = memcmp (mfp_src.header, mfp_dst.header,
		MICROFREAK_PRESET_HEADER_MSG_LEN);
  CU_ASSERT_EQUAL (err, 0);

  CU_ASSERT_EQUAL (mfp_dst.parts, mfp_src.parts);

  for (gint i = 0; i < mfp_dst.parts; i++)
    {
      err = memcmp (mfp_src.part[i], mfp_dst.part[i],
		    MICROFREAK_PRESET_PART_LEN);
      CU_ASSERT_EQUAL (err, 0);
    }
}

void
test_serialization_deserialization_init ()
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

  if (!CU_add_test
      (suite, "test_serialization_deserialization",
       test_serialization_deserialization))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "test_serialization_deserialization_init",
       test_serialization_deserialization_init))
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
