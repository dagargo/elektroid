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

void
notifier_set_dir (struct notifier *notifier)
{
  gchar *path = notifier->browser->dir;
  g_mutex_lock (&notifier->mutex);
  debug_print (1, "Changing notifier path to '%s'...\n", path);
  if (notifier->fd < 0)
    {
      g_mutex_unlock (&notifier->mutex);
      return;
    }
  if (notifier->wd >= 0)
    {
      inotify_rm_watch (notifier->fd, notifier->wd);
    }
  notifier->wd =
    inotify_add_watch (notifier->fd, path,
		       IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF
		       | IN_MOVE_SELF | IN_MOVED_TO);
  g_mutex_unlock (&notifier->mutex);
}

static void
notifier_close (struct notifier *notifier)
{
  if (notifier->fd < 0)
    {
      return;
    }
  if (notifier->wd >= 0)
    {
      inotify_rm_watch (notifier->fd, notifier->wd);
    }
  close (notifier->fd);
  notifier->fd = -1;

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
  gboolean running, active;

  while (1)
    {
      size = read (notifier->fd, notifier->event, notifier->event_size);

      g_mutex_lock (&notifier->mutex);
      running = notifier->running;
      active = notifier->active;
      g_mutex_unlock (&notifier->mutex);

      if (!running)
	{
	  break;
	}

      if (!active)
	{
	  continue;
	}

      if (size == 0 || size == EBADF)
	{
	  break;
	}

      if (size < 0)
	{
	  debug_print (2, "Error while reading notifier: %s\n",
		       g_strerror (errno));
	  continue;
	}

      if (notifier->event->mask & IN_CREATE
	  || notifier->event->mask & IN_DELETE
	  || notifier->event->mask & IN_MOVED_FROM
	  || notifier->event->mask & IN_MOVED_TO)
	{
	  debug_print (1, "Reloading local dir...\n");
	  g_idle_add (browser_load_dir, notifier->browser);
	}
      else if (notifier->event->mask & IN_DELETE_SELF
	       || notifier->event->mask & IN_MOVE_SELF ||
	       notifier->event->mask & IN_MOVED_TO)
	{
	  debug_print (1, "Loading local parent dir...\n");
	  g_idle_add (notifier_go_up, notifier->browser);
	}
      else
	{
	  if (!(notifier->event->mask & IN_IGNORED))
	    {
	      error_print ("Unexpected event: %d\n", notifier->event->mask);
	    }
	}
    }

  return NULL;
}

void
notifier_init (struct notifier *notifier, struct browser *browser)
{
  notifier->fd = inotify_init ();
  notifier->wd = -1;
  notifier->event_size = sizeof (struct inotify_event) + PATH_MAX;
  notifier->event = malloc (notifier->event_size);
  notifier->running = TRUE;
  notifier->active = FALSE;
  notifier->browser = browser;
  g_mutex_init (&notifier->mutex);
  notifier->thread = g_thread_new ("notifier", notifier_run, notifier);
}

void
notifier_set_active (struct notifier *notifier, gboolean active)
{
  g_mutex_lock (&notifier->mutex);
  notifier->active = active;
  g_mutex_unlock (&notifier->mutex);
}

void
notifier_destroy (struct notifier *notifier)
{
  g_mutex_lock (&notifier->mutex);
  notifier->running = FALSE;
  g_mutex_unlock (&notifier->mutex);
  notifier_close (notifier);
  g_thread_join (notifier->thread);
  g_free (notifier->event);
}
