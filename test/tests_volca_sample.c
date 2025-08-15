#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <sndfile.h>
#include "../src/sample.h"
#include "../src/utils.h"
#include "../src/connectors/common.h"
#include "../src/connectors/volca_sample.h"
#include "../src/connectors/volca_sample_sdk/korg_syro_volcasample.h"

gint volca_sample_get_delete (guint id, struct idata *delete_audio);

gint volca_sample_get_upload (guint id, struct idata *input,
			      struct idata *syro_op, guint32 quality,
			      struct job_control *control);

static void
test_volca_sample_compare_to (struct idata *actual, const gchar *path)
{
  gint err, cmp;
  struct idata expected;
  struct sample_info *actual_si;
  struct sample_info sample_info_req;
  struct sample_info sample_info_src;

  sample_info_init_load_direct (&sample_info_req);

  err = sample_load_from_file (path, &expected, NULL, &sample_info_req,
			       &sample_info_src);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  CU_ASSERT_EQUAL (actual->content->len, expected.content->len);
  cmp = memcmp (actual->content->data, expected.content->data,
		actual->content->len);
  CU_ASSERT_EQUAL (cmp, 0);

  actual_si = actual->info;

  CU_ASSERT_EQUAL (actual_si->frames, sample_info_src.frames);
  CU_ASSERT_EQUAL (actual_si->channels, sample_info_src.channels);
  CU_ASSERT_EQUAL (actual_si->rate, sample_info_src.rate);
  CU_ASSERT_EQUAL (actual_si->format,
		   sample_info_src.format & SF_FORMAT_SUBMASK);

  idata_free (&expected);
  idata_free (actual);
}

static void
test_volca_sample_get_delete ()
{
  gint err;
  struct idata actual;

  printf ("\n");

  err = volca_sample_get_delete (17, &actual);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  // This file has been generated with the official project and the command below.
  // syro_volcasample_example volca_sample_delete_17.wav e17:
  test_volca_sample_compare_to (&actual, TEST_DATA_DIR
				"/connectors/volca_sample_delete_17.wav");
}

static void
test_volca_sample_get_update_params (const gchar *path, guint id,
				     guint32 quality)
{
  gint err;
  struct idata sample;
  struct idata actual;
  struct job_control control;

  printf ("\n");

  controllable_init (&control.controllable);
  controllable_set_active (&control.controllable, TRUE);
  control.callback = NULL;
  job_control_reset (&control, 1);

  //Same audio file as in the
  err = common_sample_load (TEST_DATA_DIR "/connectors/square.wav",
			    &sample, NULL, 31250, 1, SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto err;
    }

  err = volca_sample_get_upload (id, &sample, &actual, quality, &control);
  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto err;
    }

  test_volca_sample_compare_to (&actual, path);

  idata_free (&sample);

err:
  controllable_clear (&control.controllable);
}

static void
test_volca_sample_get_update ()
{
  // This file has been generated with the official project and the command below.
  // Previously, square.wav was resampled to 31250 Hz.
  // syro_volcasample_example volca_sample_upload_59.wav s59:square.wav
  test_volca_sample_get_update_params (TEST_DATA_DIR
				       "/connectors/volca_sample_upload_59.wav",
				       59, 0);
}

static void
test_volca_sample_get_update_16b ()
{
  // This file has been generated with the official project and the command below.
  // Previously, square.wav was resampled to 31250 Hz.
  // syro_volcasample_example volca_sample_upload_93_16b.wav s93c16:square.wav
  test_volca_sample_get_update_params (TEST_DATA_DIR
				       "/connectors/volca_sample_upload_93_16b.wav",
				       93, 16);
}

static void
test_volca_sample_get_update_8b ()
{
  // This file has been generated with the official project and the command below.
  // Previously, square.wav was resampled to 31250 Hz.
  // syro_volcasample_example volca_sample_upload_71_8b.wav s71c8:square.wav
  test_volca_sample_get_update_params (TEST_DATA_DIR
				       "/connectors/volca_sample_upload_71_8b.wav",
				       71, 8);
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
  CU_pSuite suite = CU_add_suite ("Elektroid Volca Sample tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "volca_sample_get_delete",
		    test_volca_sample_get_delete))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "volca_sample_get_update",
		    test_volca_sample_get_update))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "volca_sample_get_update_16b",
		    test_volca_sample_get_update_16b))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "volca_sample_get_update_8b",
		    test_volca_sample_get_update_8b))
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
