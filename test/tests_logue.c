#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"
#include "../src/connectors/logue.h"

#define BYTE_COMPARISON_SIZE 16

gint logue_unit_load (const char *path, struct idata *idata,
		      struct task_control *control);

static void
test_logue_unit_load (const gchar *sysex_path, const gchar *unit_path,
		      enum logue_device device, guint8 slot)
{
  gint err;
  gchar *exp_text, *act_text;
  GByteArray *exp_payload, *act_payload;
  guint exp_payload_size, act_payload_size;
  guint8 *exp_payload_data, *act_payload_data;
  struct idata actual, expected;
  struct task_control control;

  printf ("\n");

  controllable_init (&control.controllable);
  control.callback = NULL;

  err = file_load (sysex_path, &expected, &control);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  err = logue_unit_load (unit_path, &actual, &control);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto cleanup_expected;
    }

  actual.content->data[5] = device;
  actual.content->data[8] = slot;

  // Full message

  CU_ASSERT_EQUAL (expected.content->len, actual.content->len);

  if (expected.content->len == actual.content->len)
    {
      exp_text = debug_get_hex_data (debug_level, expected.content->data,
				     expected.content->len);
      debug_print (1, "expected (%u): %s", expected.content->len, exp_text);
      g_free (exp_text);

      act_text = debug_get_hex_data (debug_level, actual.content->data,
				     actual.content->len);
      debug_print (1, "actual   (%u): %s", actual.content->len, act_text);
      g_free (act_text);

      // Payload

      exp_payload_size =
	common_midi_msg_to_8bit_msg_size (expected.content->len - 11);
      exp_payload = g_byte_array_sized_new (exp_payload_size);
      exp_payload->len = exp_payload_size;
      common_midi_msg_to_8bit_msg (&expected.content->data[9],
				   exp_payload->data,
				   expected.content->len - 11);

      exp_text = debug_get_hex_data (debug_level, exp_payload->data,
				     exp_payload->len);
      debug_print (1, "expected payload (%u): %s", exp_payload->len,
		   exp_text);
      g_free (exp_text);

      act_payload_size =
	common_midi_msg_to_8bit_msg_size (actual.content->len - 11);
      act_payload = g_byte_array_sized_new (act_payload_size);
      act_payload->len = act_payload_size;
      common_midi_msg_to_8bit_msg (&actual.content->data[9],
				   act_payload->data,
				   actual.content->len - 11);

      act_text = debug_get_hex_data (debug_level, act_payload->data,
				     act_payload->len);
      debug_print (1, "actual payload   (%u): %s", act_payload->len,
		   act_text);
      g_free (act_text);

      CU_ASSERT_EQUAL (exp_payload->len, act_payload->len);

      CU_ASSERT_EQUAL (0, memcmp (expected.content->data,
				  actual.content->data, actual.content->len));

      // 16 B payload comparison

      if (exp_payload->len == act_payload->len)
	{
	  exp_payload_data = exp_payload->data;
	  act_payload_data = act_payload->data;
	  exp_payload_size = exp_payload->len;
	  guint32 addr = 0;
	  guint line_len =
	    -(BYTE_COMPARISON_SIZE * 2 + BYTE_COMPARISON_SIZE - 1);
	  while (exp_payload_size > 0)
	    {
	      guint size = exp_payload_size >=
		BYTE_COMPARISON_SIZE ? BYTE_COMPARISON_SIZE :
		exp_payload_size;
	      exp_text = debug_get_hex_data (debug_level,
					     exp_payload_data, size);
	      act_text = debug_get_hex_data (debug_level,
					     act_payload_data, size);
	      printf ("%08x %*s | %*s -> %d\n", addr, line_len, exp_text,
		      line_len, act_text, strcmp (exp_text, act_text) == 0);
	      g_free (exp_text);
	      g_free (act_text);

	      exp_payload_size -= size;

	      exp_payload_data += size;
	      act_payload_data += size;

	      addr += BYTE_COMPARISON_SIZE;
	    }
	}

      free_msg (act_payload);
      free_msg (exp_payload);
    }

  idata_clear (&actual);
cleanup_expected:
  idata_clear (&expected);

  controllable_clear (&control.controllable);
}

// Oscillator 15: "mass" v0.01-0 api:1.01-0 did:00000000 uid:00000000

// > Target platform: "nutekt digital"
// > Target module: "Oscillator"
// > Loading "Oscillator" unit "mass" into slot #15

// size: 0x854 crc32: 8a9546d2

static void
test_logue_unit_load_1 ()
{
  test_logue_unit_load (TEST_DATA_DIR "/connectors/logue1.syx",
			TEST_DATA_DIR "/connectors/logue1.ntkdigunit",
			LOGUE_DEVICE_NTS1, 15);
}

// Modulation FX 3: "sola" v1.00-0 api:1.01-0 did:00000000 uid:00000000

// > Target platform: "nutekt digital"
// > Target module: "Modulation FX"
// > Loading "Modulation FX" unit "sola" into slot #3

// size: 0x97c crc32: fda2c831

static void
test_logue_unit_load_2 ()
{
  test_logue_unit_load (TEST_DATA_DIR "/connectors/logue2.syx",
			TEST_DATA_DIR "/connectors/logue2.ntkdigunit",
			LOGUE_DEVICE_NTS1, 3);
}

// Oscillator 10: "fm4o" v0.01-0 api:1.01-0 did:00000000 uid:00000000

// > Target platform: "nutekt digital"
// > Target module: "Oscillator"
// > Loading "Oscillator" unit "fm4o" into slot #10

// size: 0x9ac crc32: b6422e54

static void
test_logue_unit_load_3 ()
{
  test_logue_unit_load (TEST_DATA_DIR "/connectors/logue3.syx",
			TEST_DATA_DIR "/connectors/logue3.ntkdigunit",
			LOGUE_DEVICE_NTS1, 10);
}

// Delay FX 7: "rock" v1.00-0 api:1.01-0 did:00000000 uid:00000000

// > Target platform: "nutekt digital"
// > Target module: "Delay FX"
// > Loading "Delay FX" unit "rock" into slot #7

// size: 0x8a8 crc32: acfe618f

static void
test_logue_unit_load_4 ()
{
  test_logue_unit_load (TEST_DATA_DIR "/connectors/logue4.syx",
			TEST_DATA_DIR "/connectors/logue4.ntkdigunit",
			LOGUE_DEVICE_NTS1, 7);
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
  CU_pSuite suite = CU_add_suite ("Elektroid logue tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "logue_unit_load_1", test_logue_unit_load_1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "logue_unit_load_2", test_logue_unit_load_2))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "logue_unit_load_3", test_logue_unit_load_3))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "logue_unit_load_4", test_logue_unit_load_4))
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
