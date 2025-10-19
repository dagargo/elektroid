#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/audio.h"
#include "../src/sample.h"

static void
test_audio_get_previous_zero ()
{
  gint err;
  guint32 zero;
  struct idata sample;
  struct sample_info sample_info_src;
  struct sample_load_opts sample_load_opts;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square.wav",
			       &sample, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  zero = audio_get_previous_zero (&sample, 1000);
  CU_ASSERT_EQUAL (zero, 981);

  zero = audio_get_previous_zero (&sample, 0);
  CU_ASSERT_EQUAL (zero, 0);

  idata_free (&sample);
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
  CU_pSuite suite = CU_add_suite ("Elektroid audio tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "audio_get_previous_zero", test_audio_get_previous_zero))
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
