/*
 *   cz.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#ifndef MICROBRUTE_H
#define MICROBRUTE_H

#include "connector.h"

#define MICROBRUTE_NAME "microbrute"

enum microbrute_param
{
  MICROBRUTE_NOTE_PRIORITY,
  MICROBRUTE_VEL_RESPONSE,
  MICROBRUTE_LFO_KEY_RETRIGGER,
  MICROBRUTE_ENVELOPE_LEGATO,
  MICROBRUTE_BEND_RANGE,
  MICROBRUTE_GATE_LENGTH,
  MICROBRUTE_SYNC,
  MICROBRUTE_TX_CHANNEL,
  MICROBRUTE_RX_CHANNEL,
  MICROBRUTE_RETRIGGERING,
  MICROBRUTE_PLAY_ON,
  MICROBRUTE_NEXT_SEQUENCE,
  MICROBRUTE_STEP_ON,
  MICROBRUTE_STEP_LENGTH,
  MICROBRUTE_CALIB_PB_CENTER,
  MICROBRUTE_CALIB_BOTH_BOTTOM,
  MICROBRUTE_CALIB_BOTH_TOP,
  MICROBRUTE_CALIB_END
};

gint microbrute_handshake (struct backend *);

gint microbrute_get_parameter (struct backend *, enum microbrute_param,
			       guint8 *);

gint microbrute_set_parameter (struct backend *, enum microbrute_param,
			       guint8, guint8, gboolean);

extern const struct connector CONNECTOR_MICROBRUTE;

#endif
