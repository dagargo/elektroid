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

#include "connectors/cz.h"
#include "connectors/default.h"
#include "connectors/efactor.h"
#include "connectors/elektron.h"
#include "connectors/logue.h"
#include "connectors/microbrute.h"
#include "connectors/microfreak.h"
#include "connectors/monomachine.h"
#include "connectors/padkontrol.h"
#include "connectors/phatty.h"
#include "connectors/sds.h"
#include "connectors/summit.h"
#include "connectors/system.h"
#include "connectors/tanzmaus.h"
#include "connectors/volca_sample.h"
#include "connectors/volca_sample_2.h"

void
regconn_register ()
{
  // The order in this list indicates the priority of the connectors handshake.
  // Non MIDI devices (SYSTEM and NO_MIDI) are always checked first, no matter the position, as they do not need a slow MIDI handshake.
  // In order to speed up the search in this list, the regex in the connector is used against the device name.
  // Matching connectors will try handshaking first. Unmatching connectors or connectors without a regex will handshake later.
  // USB devices should use a regex in the connector for the device name. MIDI DIN devices should not as they do not have a name.
  // USB devices including a MIDI DIN port will be checked first when using the MIDI DIN port. No solution for this case.
  gslist_fill (&connectors,
	       // Fast and simple MIDI connectors go first.
	       &CONNECTOR_TANZMAUS,
	       &CONNECTOR_CZ,
	       &CONNECTOR_ELEKTRON,
	       &CONNECTOR_LOGUE,
	       &CONNECTOR_MICROBRUTE,
	       &CONNECTOR_MICROFREAK,
	       &CONNECTOR_MONOMACHINE,
	       &CONNECTOR_PADKONTROL,
	       &CONNECTOR_PHATTY,
	       &CONNECTOR_SUMMIT, &CONNECTOR_VOLCA_SAMPLE_2,
	       // sds is only tested on an E-mu ESI-2000, which is slow.
	       &CONNECTOR_SDS,
	       // But efactor is even slower even though it is USB.
	       &CONNECTOR_EFACTOR,
	       // default connector needs need to the last of the MIDI connectors as the handshake always succeeds.
	       &CONNECTOR_DEFAULT,
	       // Non MIDI connectors can go anywhere but let's add it to the end.
	       &CONNECTOR_SYSTEM, &CONNECTOR_VOLCA_SAMPLE, NULL);
}

void
regconn_unregister ()
{
  g_slist_free (g_steal_pointer (&connectors));
}
