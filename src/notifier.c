/*
 *   notifier.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Elektroid.
 *
 *   Elektroid is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Elektroid is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Elektroid. If not, see <http://www.gnu.org/licenses/>.
 */

#include "notifier.h"

#define NOTIFIER_RATE_LIMIT_MS 1000

static gboolean
notifier_reset (gpointer data)
{
  struct browser *browser = data;
  browser_cancel (browser);
  browser->dir = get_system_startup_path (NULL);
  browser_load_dir (browser);
  return FALSE;
}

static void
notifier_changed (GFileMonitor *self, GFile *file, GFile *other_file,
		  GFileMonitorEvent event_type, gpointer user_data)
{
  struct notifier *notifier = user_data;
  gchar *p1 = g_file_get_path (notifier->dir);
  gchar *p2 = g_file_get_path (file);
  gboolean itself = strcmp (p1, p2) == 0;

  g_free (p1);
  g_free (p2);

  debug_print (2, "Processing notifier change (itself =?= %d)...", itself);

  if (itself)
    {
      if (event_type == G_FILE_MONITOR_EVENT_DELETED)
	{
	  debug_print (1, "Processing notifier deleted dir...");
	  g_idle_add (notifier_reset, notifier->browser);
	}
    }
  else
    {
      if (event_type == G_FILE_MONITOR_EVENT_DELETED ||
	  event_type == G_FILE_MONITOR_EVENT_CREATED)
	{
	  debug_print (1, "Processing notifier reload...");
	  g_idle_add (browser_load_dir, notifier->browser);
	}
    }
}

void
notifier_init (struct notifier **notifier, struct browser *browser)
{
  struct notifier *n = g_malloc (sizeof (struct notifier));
  n->monitor = NULL;
  n->browser = browser;
  *notifier = n;
}

void
notifier_update_dir (struct notifier *notifier, gboolean active)
{
  debug_print (1, "Changing %s browser path to '%s'...",
	       notifier->browser->name, notifier->browser->dir);

  if (notifier->monitor)
    {
      g_object_unref (notifier->monitor);
      g_object_unref (notifier->dir);

      notifier->monitor = NULL;
      notifier->dir = NULL;
    }

  if (active && strcmp (notifier->browser->dir, TOPMOST_DIR_WINDOWS))
    {
      notifier->dir = g_file_new_for_path (notifier->browser->dir);
      notifier->monitor = g_file_monitor_directory (notifier->dir,
						    G_FILE_MONITOR_NONE,
						    NULL, NULL);
      g_file_monitor_set_rate_limit (notifier->monitor,
				     NOTIFIER_RATE_LIMIT_MS);
      g_signal_connect (notifier->monitor, "changed",
			G_CALLBACK (notifier_changed), notifier);
    }
}

void
notifier_destroy (struct notifier *notifier)
{
  if (notifier->monitor)
    {
      g_object_unref (notifier->monitor);
      g_object_unref (notifier->dir);
    }

  g_free (notifier);
}
