#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/utils.h"
#include "../src/connectors/common.h"

#define MONOMACHINE_WAVE_MSG_LEN 7027
#define MONOMACHINE_WAVE_FRAMES 2044
#define MONOMACHINE_NAME_LEN 4

guint16 monomachine_wave_msg_checksum (const guint8 * msg);

void monomachine_sanitize_name (const gchar * src, gchar * dst);

GByteArray *monomachine_wave_msg_new (guint id, struct idata *sample);

gint monomachine_wave_msg_parse (GByteArray * rx_msg, guint id,
				 struct idata *sample);

gboolean
elektroid_ask_user_to_continue (const gchar *msg,
				struct controllable *controllable)
{
  return TRUE;
}

static void
tests_monomachine_new_sample (struct idata *sample, const gchar *name,
			      guint frames)
{
  gint16 v;
  GByteArray *content = g_byte_array_new ();
  struct sample_info *sample_info = sample_info_new (FALSE);

  for (guint i = 0; i < frames; i++)
    {
      v = (gint16) ((i * 13) % 65536 - 32768);
      g_byte_array_append (content, (guint8 *) & v, sizeof (gint16));
    }

  sample_info->frames = frames;
  sample_info->rate = 44100;
  sample_info->channels = 1;
  sample_info->format = SF_FORMAT_PCM_16;

  idata_init (sample, content, g_strdup (name), sample_info,
	      sample_info_free);
}

static void
test_sanitize_name ()
{
  gchar name[MONOMACHINE_NAME_LEN + 1];

  printf ("\n");

  monomachine_sanitize_name (NULL, name);
  CU_ASSERT_STRING_EQUAL (name, "    ");

  monomachine_sanitize_name ("bass drum", name);
  CU_ASSERT_STRING_EQUAL (name, "BASS");

  monomachine_sanitize_name ("ab", name);
  CU_ASSERT_STRING_EQUAL (name, "AB  ");

  monomachine_sanitize_name ("x\x7fyz", name);
  CU_ASSERT_STRING_EQUAL (name, "X YZ");
}

static void
test_wave_msg_new ()
{
  struct idata sample;
  GByteArray *msg;
  guint16 cksum;
  const guint8 header[] = { 0xf0, 0, 0x20, 0x3c, 3, 0, 0x5d, 1, 1 };

  printf ("\n");

  tests_monomachine_new_sample (&sample, "ramp waveform",
				MONOMACHINE_WAVE_FRAMES);
  msg = monomachine_wave_msg_new (7, &sample);

  CU_ASSERT_EQUAL (msg->len, MONOMACHINE_WAVE_MSG_LEN);
  CU_ASSERT_EQUAL (memcmp (msg->data, header, sizeof (header)), 0);
  CU_ASSERT_EQUAL (msg->data[9], 7);
  CU_ASSERT_EQUAL (memcmp (&msg->data[10], "RAMP", MONOMACHINE_NAME_LEN), 0);
  //Length field: 7017 as two septets.
  CU_ASSERT_EQUAL (msg->data[7024], 0x36);
  CU_ASSERT_EQUAL (msg->data[7025], 0x69);
  CU_ASSERT_EQUAL (msg->data[7026], 0xf7);

  cksum = monomachine_wave_msg_checksum (msg->data);
  CU_ASSERT_EQUAL (msg->data[7022], cksum >> 7);
  CU_ASSERT_EQUAL (msg->data[7023], cksum & 0x7f);

  //Everything between the SysEx markers must be 7 bit clean.
  for (guint i = 1; i < msg->len - 1; i++)
    {
      if (msg->data[i] > 0x7f)
	{
	  CU_FAIL ("Byte over 0x7f in message payload");
	  break;
	}
    }

  free_msg (msg);
  idata_clear (&sample);
}

static void
test_wave_msg_roundtrip ()
{
  struct idata sample, decoded;
  GByteArray *msg;
  struct sample_info *sample_info;
  gint err;

  printf ("\n");

  tests_monomachine_new_sample (&sample, "RAMP", MONOMACHINE_WAVE_FRAMES);
  msg = monomachine_wave_msg_new (3, &sample);
  err = monomachine_wave_msg_parse (msg, 3, &decoded);

  CU_ASSERT_EQUAL (err, 0);
  if (!err)
    {
      sample_info = decoded.info;
      CU_ASSERT_EQUAL (sample_info->frames, MONOMACHINE_WAVE_FRAMES);
      CU_ASSERT_EQUAL (sample_info->channels, 1);
      CU_ASSERT_STRING_EQUAL (decoded.name, "RAMP");
      CU_ASSERT_EQUAL (decoded.content->len, sample.content->len);
      CU_ASSERT_EQUAL (memcmp (decoded.content->data, sample.content->data,
			       sample.content->len), 0);
      idata_clear (&decoded);
    }

  free_msg (msg);
  idata_clear (&sample);
}

static void
test_wave_msg_pad ()
{
  struct idata sample, decoded;
  GByteArray *msg;
  gint16 *frames;
  gint err;

  printf ("\n");

  tests_monomachine_new_sample (&sample, "PAD", 10);
  msg = monomachine_wave_msg_new (0, &sample);
  err = monomachine_wave_msg_parse (msg, 0, &decoded);

  CU_ASSERT_EQUAL (err, 0);
  if (!err)
    {
      CU_ASSERT_EQUAL (decoded.content->len,
		       MONOMACHINE_WAVE_FRAMES * sizeof (gint16));
      CU_ASSERT_EQUAL (memcmp (decoded.content->data, sample.content->data,
			       sample.content->len), 0);
      frames = (gint16 *) decoded.content->data;
      for (guint i = 10; i < MONOMACHINE_WAVE_FRAMES; i++)
	{
	  if (frames[i])
	    {
	      CU_FAIL ("Padding frame not zero");
	      break;
	    }
	}
      idata_clear (&decoded);
    }

  free_msg (msg);
  idata_clear (&sample);
}

static void
test_wave_msg_parse_errors ()
{
  struct idata sample, decoded;
  GByteArray *msg;

  printf ("\n");

  tests_monomachine_new_sample (&sample, "ERR", MONOMACHINE_WAVE_FRAMES);
  msg = monomachine_wave_msg_new (5, &sample);

  //Wrong slot.
  CU_ASSERT_NOT_EQUAL (monomachine_wave_msg_parse (msg, 6, &decoded), 0);

  //Wrong message ID.
  msg->data[6] = 0x50;
  CU_ASSERT_NOT_EQUAL (monomachine_wave_msg_parse (msg, 5, &decoded), 0);
  msg->data[6] = 0x5d;

  //Corrupted payload breaks the checksum.
  msg->data[100] ^= 1;
  CU_ASSERT_NOT_EQUAL (monomachine_wave_msg_parse (msg, 5, &decoded), 0);
  msg->data[100] ^= 1;

  //Truncated message.
  g_byte_array_set_size (msg, 100);
  CU_ASSERT_NOT_EQUAL (monomachine_wave_msg_parse (msg, 5, &decoded), 0);

  free_msg (msg);
  idata_clear (&sample);
}

gint
main (gint argc, gchar *argv[])
{
  gint err = 0;

  debug_level = 1;

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("Elektroid Monomachine tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "sanitize_name", test_sanitize_name))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "wave_msg_new", test_wave_msg_new))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "wave_msg_roundtrip", test_wave_msg_roundtrip))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "wave_msg_pad", test_wave_msg_pad))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "wave_msg_parse_errors",
		    test_wave_msg_parse_errors))
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
