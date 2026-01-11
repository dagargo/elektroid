/*
 *   logue.h
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
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

#ifndef LOGUE_H
#define LOGUE_H

#include "connector.h"

enum logue_device
{
  LOGUE_DEVICE_PROLOGUE = 0x4b,
  LOGUE_DEVICE_MINILOGUE_XD = 0x51,
  LOGUE_DEVICE_NTS1 = 0x57
};

enum logue_module
{
  FS_LOGUE_MODULE_MODFX = 1,
  FS_LOGUE_MODULE_DELFX,
  FS_LOGUE_MODULE_REVFX,
  FS_LOGUE_MODULE_OSC,
};

extern const struct connector CONNECTOR_LOGUE;

#endif
