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

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include "notifier.h"
#include "utils.h"

static void
notifier_set_dir (struct notifier *notifier)
{
  debug_print (1, "Changing %s browser path to '%s'...\n",
	       notifier->browser->name, notifier->browser->dir);
  if (!notifier->dir || strcmp (notifier->browser->dir, notifier->dir))
    {
      if (notifier->dir)
	{
	  g_free (notifier->dir);
	  inotify_rm_watch (notifier->fd, notifier->wd);
	  g_thread_join (notifier->thread);
	  notifier->thread = NULL;
	}
      notifier->dir = strdup (notifier->browser->dir);
      notifier->wd = inotify_add_watch (notifier->fd, notifier->dir,
					IN_CREATE | IN_DELETE | IN_MOVED_FROM
					| IN_DELETE_SELF | IN_MOVE_SELF
					| IN_MOVED_TO | IN_IGNORED);
    }
}

static gboolean
notifier_go_up (gpointer data)
{
  struct browser *browser = data;
  browser_go_up (NULL, browser);
  return FALSE;
}

static gpointer
notifier_run (gpointer data)
{
  ssize_t size;
  struct notifier *notifier = data;

  debug_print (1, "%s notifier running...\n", notifier->browser->name);

  while (1)
    {
      size = read (notifier->fd, notifier->event, notifier->event_size);
      if (size == 0)
	{
	  break;
	}
      if (size == -1)
	{
	  if (errno != EBADF)
	    {
	      debug_print (2, "%s\n", g_strerror (errno));
	    }
	  break;
	}

      if (notifier->event->mask & IN_CREATE
	  || notifier->event->mask & IN_DELETE
	  || notifier->event->mask & IN_MOVED_FROM
	  || notifier->event->mask & IN_MOVED_TO)
	{
	  debug_print (1, "Reloading dir...\n");
	  g_idle_add (browser_load_dir, notifier->browser);
	}
      else if (notifier->event->mask & IN_DELETE_SELF
	       || notifier->event->mask & IN_MOVE_SELF
	       || notifier->event->mask & IN_MOVED_TO)
	{
	  debug_print (1, "Loading parent dir...\n");
	  g_idle_add (notifier_go_up, notifier->browser);
	  break;		//There is no directory to be nofified of.
	}
      else if ((notifier->event->mask & IN_IGNORED))	// inotify_rm_watch called
	{
	  debug_print (1, "Finishing notifier...\n");
	  break;
	}
      else
	{
	  error_print ("Unexpected event: %d\n", notifier->event->mask);
	}
    }

  return NULL;
}

void
notifier_init (struct notifier *notifier, struct browser *browser)
{
  notifier->fd = inotify_init ();
  notifier->event_size = sizeof (struct inotify_event) + PATH_MAX;
  notifier->event = malloc (notifier->event_size);
  notifier->browser = browser;
  notifier->thread = NULL;
  notifier->dir = NULL;
  g_mutex_init (&notifier->mutex);
}

void
notifier_set_active (struct notifier *notifier, gboolean active)
{
  g_mutex_lock (&notifier->mutex);
  if (active)
    {
      notifier_set_dir (notifier);
      if (!notifier->thread)
	{
	  debug_print (1, "Starting %s notifier...\n",
		       notifier->browser->name);
	  notifier->thread = g_thread_new ("notifier", notifier_run,
					   notifier);
	}
    }
  else
    {
      if (notifier->thread)
	{
	  debug_print (1, "Stopping %s notifier...\n",
		       notifier->browser->name);
	  inotify_rm_watch (notifier->fd, notifier->wd);
	  g_thread_join (notifier->thread);
	  notifier->thread = NULL;
	  close (notifier->fd);
	}
    }
  g_mutex_unlock (&notifier->mutex);
}

void
notifier_destroy (struct notifier *notifier)
{
  notifier_set_active (notifier, FALSE);
  g_free (notifier->event);
}
