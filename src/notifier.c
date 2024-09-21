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

static gboolean
notifier_go_up (gpointer data)
{
  struct browser *browser = data;
  browser_go_up (NULL, browser);
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

  debug_print (1, "Processing notifier change...");

  if (event_type == G_FILE_MONITOR_EVENT_DELETED && itself)
    {
      g_idle_add (notifier_go_up, notifier->browser);
    }
  else
    {
      g_idle_add (browser_load_dir, notifier->browser);
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
    }

  if (active)
    {
      notifier->dir = g_file_new_for_path (notifier->browser->dir);
      notifier->monitor = g_file_monitor_directory (notifier->dir,
						    G_FILE_MONITOR_NONE, NULL,
						    NULL);
      g_file_monitor_set_rate_limit (notifier->monitor, 200);
      g_signal_connect (notifier->monitor, "changed",
			G_CALLBACK (notifier_changed), notifier);
    }
  else
    {
      notifier->monitor = NULL;
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
