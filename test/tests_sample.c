#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <glib/gstdio.h>
#include "../src/sample.h"
#include "../src/preferences.h"

static void
test_load_sample_resampling (struct task_control *control)
{
  gint err;
  struct idata sample;
  struct sample_info *sample_info, sample_info_src;
  struct sample_load_opts sample_load_opts;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square-wav-stereo-44k1-8b.wav",
			       &sample, control, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

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

  CU_ASSERT_EQUAL (sample.content->len, sample_info->frames * 2);

  CU_ASSERT_EQUAL (0, memcmp (sample.content->data,
			      "\xa3\x03\x49\x4f\xeb\x6a\x51\x62", 8));

  idata_free (&sample);
}

static void
test_load_sample_control_resampling ()
{
  struct task_control task_control;
  controllable_init (&task_control.controllable);
  task_control.callback = NULL;
  test_load_sample_resampling (&task_control);
  controllable_clear (&task_control.controllable);
}

static void
test_load_sample_no_control_resampling ()
{
  test_load_sample_resampling (NULL);
}

static void
test_load_sample_no_resampling (struct task_control *control)
{
  gint err;
  struct idata sample;
  struct sample_info *sample_info, sample_info_src;
  struct sample_load_opts sample_load_opts;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square-wav-mono-48k-16b.wav",
			       &sample, control, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  CU_ASSERT_EQUAL (sample_info_src.frames, 48000);
  CU_ASSERT_EQUAL (sample_info_src.loop_start, 6331);
  CU_ASSERT_EQUAL (sample_info_src.loop_end, 43312);
  CU_ASSERT_EQUAL (sample_info_src.loop_type, 0x7f);
  CU_ASSERT_EQUAL (sample_info_src.rate, 48000);
  CU_ASSERT_EQUAL (sample_info_src.format, SF_FORMAT_WAV | SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info_src.channels, 1);
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

  CU_ASSERT_EQUAL (sample.content->len, sample_info->frames * 2);

  CU_ASSERT_EQUAL (0, memcmp (sample.content->data,
			      "\xff\xff\x8d\x53\xc8\x67\x1d\x66", 8));

  idata_free (&sample);
}

static void
test_load_sample_control_no_resampling ()
{
  struct task_control task_control;
  controllable_init (&task_control.controllable);
  task_control.callback = NULL;
  test_load_sample_no_resampling (&task_control);
  controllable_clear (&task_control.controllable);
}

static void
test_load_sample_no_control_no_resampling ()
{
  test_load_sample_no_resampling (NULL);
}

static void
test_load_microfreak_wavetable (const gchar *path)
{
  gint err;
  struct idata sample;
  struct sample_info *sample_info, sample_info_src;
  struct sample_load_opts sample_load_opts;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);

  err = sample_load_from_file (path, &sample, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  CU_ASSERT_EQUAL (sample_info_src.frames, 8 * KI);
  CU_ASSERT_EQUAL (sample_info_src.loop_start, 0);
  CU_ASSERT_EQUAL (sample_info_src.loop_end, 8191);
  CU_ASSERT_EQUAL (sample_info_src.loop_type, 0);
  CU_ASSERT_EQUAL (sample_info_src.rate, 32000);
  CU_ASSERT_EQUAL (sample_info_src.format,
		   ELEKTROID_SAMPLE_FORMAT_MICROFREAK | SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info_src.channels, 1);
  CU_ASSERT_EQUAL (sample_info_src.midi_note, 0);

  sample_info = sample.info;
  CU_ASSERT_EQUAL (sample_info->frames, 12288);
  CU_ASSERT_EQUAL (sample_info->loop_start, 0);
  CU_ASSERT_EQUAL (sample_info->loop_end, 12287);
  CU_ASSERT_EQUAL (sample_info->loop_type, 0);
  CU_ASSERT_EQUAL (sample_info->rate, 48000);
  CU_ASSERT_EQUAL (sample_info->format, SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info->channels, 1);
  CU_ASSERT_EQUAL (sample_info->midi_note, 0);

  CU_ASSERT_EQUAL (sample.content->len, sample_info->frames * 2);

  CU_ASSERT_EQUAL (0, memcmp (sample.content->data,
			      "\x40\xdc\x6b\xd7\x85\xdd\xbf\xdb", 8));

  idata_free (&sample);
}

static void
test_load_microfreak_mfw ()
{
  test_load_microfreak_wavetable (TEST_DATA_DIR "/connectors/microfreak.mfw");
}

static void
test_load_microfreak_mfwz ()
{
  test_load_microfreak_wavetable (TEST_DATA_DIR
				  "/connectors/microfreak.mfwz");
}

static void
test_load_microfreak_sample (const gchar *path)
{
  gint err;
  struct idata sample;
  struct sample_info *sample_info, sample_info_src;
  struct sample_load_opts sample_load_opts;

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16,
			 FALSE);

  err = sample_load_from_file (path, &sample, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  CU_ASSERT_EQUAL (sample_info_src.frames, 800);
  CU_ASSERT_EQUAL (sample_info_src.loop_start, 0);
  CU_ASSERT_EQUAL (sample_info_src.loop_end, 799);
  CU_ASSERT_EQUAL (sample_info_src.loop_type, 0);
  CU_ASSERT_EQUAL (sample_info_src.rate, 32000);
  CU_ASSERT_EQUAL (sample_info_src.format,
		   ELEKTROID_SAMPLE_FORMAT_MICROFREAK | SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info_src.channels, 1);
  CU_ASSERT_EQUAL (sample_info_src.midi_note, 0);

  sample_info = sample.info;
  CU_ASSERT_EQUAL (sample_info->frames, 1200);
  CU_ASSERT_EQUAL (sample_info->loop_start, 0);
  CU_ASSERT_EQUAL (sample_info->loop_end, 1199);
  CU_ASSERT_EQUAL (sample_info->loop_type, 0);
  CU_ASSERT_EQUAL (sample_info->rate, 48000);
  CU_ASSERT_EQUAL (sample_info->format, SF_FORMAT_PCM_16);
  CU_ASSERT_EQUAL (sample_info->channels, 1);
  CU_ASSERT_EQUAL (sample_info->midi_note, 0);

  CU_ASSERT_EQUAL (sample.content->len, sample_info->frames * 2);

  CU_ASSERT_EQUAL (0, memcmp (sample.content->data,
			      "\xc9\x4c\xe3\x56\x61\x49\xce\x4c", 8));

  idata_free (&sample);
}

static void
test_load_microfreak_mfs ()
{
  test_load_microfreak_sample (TEST_DATA_DIR "/connectors/microfreak.mfs");
}

static void
test_load_microfreak_mfsz ()
{
  test_load_microfreak_sample (TEST_DATA_DIR "/connectors/microfreak.mfsz");
}

// This test checks for transparency. No changes in the file even when loading the tags as there are none.

static void
test_load_and_save_no_tags ()
{
  gint err;
  struct idata sample, f1, f2;
  struct sample_info sample_info_src;
  struct sample_load_opts sample_load_opts;
  const gchar *dst = "foo.wav";

  printf ("\n");

  err = file_load (TEST_DATA_DIR "/connectors/square.wav", &f1, NULL);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16, TRUE);

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square.wav",
			       &sample, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto free_f1;
    }

  err = sample_save_to_file (dst, &sample, NULL, sample_info_src.format);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto free_sample;
    }


  err = file_load (dst, &f2, NULL);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto unlink_dst;
    }

  CU_ASSERT_EQUAL (f1.content->len, f2.content->len);
  CU_ASSERT_EQUAL (0, memcmp (f1.content->data, f2.content->data,
			      f1.content->len));

  idata_free (&f2);
unlink_dst:
  g_unlink (dst);
free_sample:
  idata_free (&sample);
free_f1:
  idata_free (&f1);
}

static void
test_load_save_with_tag_and_reload ()
{
  gint err;
  struct idata s1, s2;
  struct sample_info *sample_info, sample_info_src;
  struct sample_load_opts sample_load_opts;
  const gchar *dst = "foo.wav";

  printf ("\n");

  sample_load_opts_init (&sample_load_opts, 1, 48000, SF_FORMAT_PCM_16, TRUE);

  err = sample_load_from_file (TEST_DATA_DIR
			       "/connectors/square.wav",
			       &s1, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      return;
    }

  sample_info = s1.info;
  sample_info_set_tag (sample_info, "IKEY", strdup ("loop; FX"));
  sample_info_set_tag (sample_info, "key", strdup ("x"));	//This does not work as keys need to be 4 byte long

  err = sample_save_to_file (dst, &s1, NULL, sample_info_src.format);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto free_s1;
    }

  err = sample_load_from_file (dst, &s2, NULL, &sample_load_opts,
			       &sample_info_src);

  CU_ASSERT_EQUAL (err, 0);
  if (err)
    {
      goto unlink_dst;
    }

  sample_info = s2.info;
  CU_ASSERT_NOT_EQUAL (sample_info->tags, NULL);
  CU_ASSERT_STRING_EQUAL (sample_info_get_tag (sample_info, "IKEY"),
			  "loop; FX");
  CU_ASSERT_EQUAL (sample_info_get_tag (sample_info, "key"), NULL);

  idata_free (&s2);
unlink_dst:
  g_unlink (dst);
free_s1:
  idata_free (&s1);
}

static gint
run_tests (CU_pSuite suite)
{
  if (!CU_add_test (suite, "load_sample_control_resampling",
		    test_load_sample_control_resampling))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_sample_no_control_resampling",
		    test_load_sample_no_control_resampling))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_sample_control_no_resampling",
		    test_load_sample_control_no_resampling))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_sample_no_control_no_resampling",
		    test_load_sample_no_control_no_resampling))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_microfreak_mfw", test_load_microfreak_mfw))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_microfreak_mfwz", test_load_microfreak_mfwz))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_microfreak_mfs", test_load_microfreak_mfs))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_microfreak_mfsz", test_load_microfreak_mfsz))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_and_save_no_tags",
		    test_load_and_save_no_tags))
    {
      return -1;
    }

  if (!CU_add_test (suite, "load_save_with_tag_and_reload",
		    test_load_save_with_tag_and_reload))
    {
      return -1;
    }

  return 0;
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

  preferences_hashtable = g_hash_table_new_full (g_str_hash, g_str_equal,
						 NULL, g_free);
  preferences_set_boolean (PREF_KEY_AUDIO_USE_FLOAT, TRUE);

  if (run_tests (suite))
    {
      goto cleanup;
    }

  preferences_set_boolean (PREF_KEY_AUDIO_USE_FLOAT, FALSE);

  if (run_tests (suite))
    {
      goto cleanup;
    }

  CU_basic_set_mode (CU_BRM_VERBOSE);

  CU_basic_run_tests ();
  err = CU_get_number_of_tests_failed ();

cleanup:
  preferences_free ();
  CU_cleanup_registry ();
  return err || CU_get_error ();
}
