#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../config.h"
#include "../src/utils.h"
#include "../src/connectors/package.h"
#include "../src/connectors/elektron.h"

static struct backend backend;
static struct elektron_data data;
static struct fs_operations ops;

void
test_elektron_get_dev_exts ()
{
  GSList *exts;

  printf ("\n");

  exts = elektron_get_dev_exts (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 1);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);

  g_slist_free_full (exts, g_free);
}

void
test_elektron_get_dev_exts_pst ()
{
  GSList *exts;

  printf ("\n");

  data.device_desc.id = 0;
  exts = elektron_get_dev_exts_pst (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 1);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);

  g_slist_free_full (exts, g_free);

  data.device_desc.id = 32;
  exts = elektron_get_dev_exts_pst (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 2);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);
  CU_ASSERT_EQUAL (strcmp (exts->next->data, "ahpst"), 0);

  g_slist_free_full (exts, g_free);
}

void
test_elektron_get_dev_exts_prj ()
{
  GSList *exts;

  printf ("\n");

  data.device_desc.id = 0;
  exts = elektron_get_dev_exts_prj (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 1);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);

  g_slist_free_full (exts, g_free);

  data.device_desc.id = 42;
  exts = elektron_get_dev_exts_prj (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 2);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);
  CU_ASSERT_EQUAL (strcmp (exts->next->data, "dtprj"), 0);

  g_slist_free_full (exts, g_free);

  data.device_desc.id = 43;
  exts = elektron_get_dev_exts_prj (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 2);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);
  CU_ASSERT_EQUAL (strcmp (exts->next->data, "dnprj"), 0);

  g_slist_free_full (exts, g_free);
}

void
test_elektron_get_takt_ii_pst_exts ()
{
  GSList *exts;

  printf ("\n");

  data.device_desc.id = 0;
  exts = elektron_get_takt_ii_pst_exts (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 1);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);

  g_slist_free_full (exts, g_free);

  data.device_desc.id = 42;
  exts = elektron_get_takt_ii_pst_exts (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 2);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);
  CU_ASSERT_EQUAL (strcmp (exts->next->data, "dtsnd"), 0);

  g_slist_free_full (exts, g_free);

  data.device_desc.id = 43;
  exts = elektron_get_takt_ii_pst_exts (&backend, &ops);

  CU_ASSERT_EQUAL (g_slist_length (exts), 2);
  CU_ASSERT_EQUAL (strcmp (exts->data, "aliasextension"), 0);
  CU_ASSERT_EQUAL (strcmp (exts->next->data, "dnsnd"), 0);

  g_slist_free_full (exts, g_free);
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
  CU_pSuite suite = CU_add_suite ("Elektroid elektron tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  backend.data = &data;
  snprintf (data.device_desc.alias, LABEL_MAX, "%s", "alias");
  ops.ext = "extension";

  if (!CU_add_test (suite, "elektron_get_dev_exts",
		    test_elektron_get_dev_exts))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_get_dev_exts_pst",
		    test_elektron_get_dev_exts_pst))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_get_dev_exts_prj",
		    test_elektron_get_dev_exts_prj))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "elektron_get_takt_ii_pst_exts",
		    test_elektron_get_takt_ii_pst_exts))
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
