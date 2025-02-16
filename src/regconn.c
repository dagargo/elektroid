/*
 *   regconn.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include "regconn.h"

#include "connectors/system.h"
#include "connectors/elektron.h"
#include "connectors/microbrute.h"
#include "connectors/microfreak.h"
#include "connectors/cz.h"
#include "connectors/sds.h"
#include "connectors/efactor.h"
#include "connectors/phatty.h"
#include "connectors/summit.h"
#include "connectors/default.h"

void
regconn_register ()
{
  system_connector = &CONNECTOR_SYSTEM;
  gslist_fill (&connectors, &CONNECTOR_MICROBRUTE,
	       &CONNECTOR_MICROFREAK, &CONNECTOR_PHATTY, &CONNECTOR_SUMMIT,
	       &CONNECTOR_CZ, &CONNECTOR_SDS, &CONNECTOR_EFACTOR,
	       &CONNECTOR_DEFAULT, NULL);
}

void
regconn_unregister ()
{
  g_slist_free (g_steal_pointer (&connectors));
}
