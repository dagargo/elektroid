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

#define NOTIFIER_BATCH_EVENTS 256
#define NOTIFIER_REST_TIME_US 250000

#if defined(__linux__)
static void
notifier_set_dir (struct notifier *notifier)
{
  if (!notifier->dir || strcmp (notifier->browser->dir, notifier->dir))
    {
      debug_print (1, "Changing %s browser path to '%s'...\n",
		   notifier->browser->name, notifier->browser->dir);
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
					| IN_MOVED_TO | IN_IGNORED |
					IN_ATTRIB);
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
  gboolean reload = FALSE;
  struct notifier *notifier = data;

  debug_print (1, "%s notifier running...\n", notifier->browser->name);

  while (1)
    {
      size = read (notifier->fd, notifier->event, notifier->event_size);
      if (size == -1)
	{
	  if (errno == EAGAIN)
	    {
	      if (reload)
		{
		  debug_print (1, "Adding browser load function...\n");
		  g_idle_add (browser_load_dir, notifier->browser);
		  reload = FALSE;
		}
	      usleep (NOTIFIER_REST_TIME_US);
	      continue;
	    }
	  break;
	}

      struct inotify_event *e = notifier->event;
      for (gint i = 0; i < NOTIFIER_BATCH_EVENTS; i++, e++)
	{
	  if (notifier->event->mask & IN_CREATE
	      || notifier->event->mask & IN_DELETE
	      || notifier->event->mask & IN_MOVED_FROM
	      || notifier->event->mask & IN_MOVED_TO
	      || notifier->event->mask & IN_ATTRIB)
	    {
	      reload = TRUE;
	    }
	  else if (notifier->event->mask & IN_DELETE_SELF
		   || notifier->event->mask & IN_MOVE_SELF
		   || notifier->event->mask & IN_MOVED_TO)
	    {
	      debug_print (1, "Loading parent dir...\n");
	      g_idle_add (notifier_go_up, notifier->browser);
	      goto end;		//There is no directory to be nofified of.
	    }
	  else if ((notifier->event->mask & IN_IGNORED))	// inotify_rm_watch called
	    {
	      debug_print (1, "Finishing notifier...\n");
	      goto end;
	    }
	  else
	    {
	      error_print ("Unexpected event: %d\n", notifier->event->mask);
	    }
	}
    }

end:
  return NULL;
}
#endif

void
notifier_init (struct notifier *notifier, struct browser *browser)
{
#if defined(__linux__)
  notifier->fd = inotify_init1 (IN_NONBLOCK);
  notifier->event_size =
    (sizeof (struct inotify_event) + NAME_MAX + 1) * NOTIFIER_BATCH_EVENTS;
  notifier->event = g_malloc (notifier->event_size);
  notifier->browser = browser;
  notifier->thread = NULL;
  notifier->dir = NULL;
  g_mutex_init (&notifier->mutex);
#endif
}

void
notifier_set_active (struct notifier *notifier, gboolean active)
{
#if defined(__linux__)
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
#endif
}

void
notifier_destroy (struct notifier *notifier)
{
#if defined(__linux__)
  notifier_set_active (notifier, FALSE);
  g_free (notifier->event);
#endif
}
