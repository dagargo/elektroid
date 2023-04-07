/*
 *   notifier.h
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

#ifndef NOTIFIER_H
#define NOTIFIER_H

#if defined(__linux__)
#include <sys/inotify.h>
#endif
#include <gtk/gtk.h>
#include "browser.h"

struct notifier
{
#if defined(__linux__)
  gchar *dir;
  gint fd;
  gint wd;
  size_t event_size;
  struct inotify_event *event;
  struct browser *browser;
  GThread *thread;
  GMutex mutex;
#endif
};

void notifier_init (struct notifier *, struct browser *);

void notifier_set_active (struct notifier *, gboolean);

void notifier_destroy (struct notifier *);

#endif
