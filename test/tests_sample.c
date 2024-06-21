#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/sample.h"

void
test_load_sample (struct job_control *control)
{
  gint err;
  struct idata sample;
  struct sample_info sample_info_req;
  struct sample_info sample_info_src;
  struct sample_info *sample_info;

  printf ("\n");

  sample_info_req.channels = 1;
  sample_info_req.rate = 48000;
  sample_info_req.format = SF_FORMAT_PCM_16;

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square-wav44.1k8b2c.wav", &sample,
			       control, &sample_info_req, &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);

  CU_ASSERT_EQUAL (sample_info_src.frames, 44100);
  CU_ASSERT_EQUAL (sample_info_src.loop_start, 5817);
  CU_ASSERT_EQUAL (sample_info_src.loop_end, 39793);
  CU_ASSERT_EQUAL (sample_info_src.loop_type, 0x7f);
  CU_ASSERT_EQUAL (sample_info_src.rate, 44100);
  CU_ASSERT_EQUAL (sample_info_src.format, SF_FORMAT_WAV | SF_FORMAT_PCM_U8);
  CU_ASSERT_EQUAL (sample_info_src.channels, 2);
  CU_ASSERT_EQUAL (sample_info_src.midi_note, 0);

  sample_info = sample.info;
  CU_ASSERT_EQUAL (sample_info->frames, 48000);
  CU_ASSERT_EQUAL (sample_info->loop_start, 6331);
  CU_ASSERT_EQUAL (sample_info->loop_end, 43312);
  CU_ASSERT_EQUAL (sample_info->loop_type, 0x7f);
  CU_ASSERT_EQUAL (sample_info->rate, 48000);
  CU_ASSERT_EQUAL (sample_info->format, SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info->channels, 1);
  CU_ASSERT_EQUAL (sample_info->midi_note, 0);

  idata_free (&sample);
}

void
test_load_sample_control ()
{
  struct job_control control;
  control.active = TRUE;
  control.callback = NULL;
  g_mutex_init (&control.mutex);
  test_load_sample (&control);
}

void
test_load_sample_no_control ()
{
  test_load_sample (NULL);
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
  CU_pSuite suite = CU_add_suite ("Elektroid sample tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "load_sample_control", test_load_sample_control))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "load_sample_no_control",
		    test_load_sample_no_control))
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
