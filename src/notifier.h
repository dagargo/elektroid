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

#include <glib.h>
#include "browser.h"

struct notifier
{
  GFile *dir;
  GFileMonitor *monitor;
  struct browser *browser;
};

void notifier_init (struct notifier **notifier, struct browser *browser);

void notifier_update_dir (struct notifier *, gboolean active);

void notifier_destroy (struct notifier *notifier);

#endif
