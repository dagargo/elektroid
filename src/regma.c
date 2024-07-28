/*
 *   regma.c
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

#include "regma.h"

struct maction *autosampler_maction_builder (struct maction_context *);
struct maction *backend_maction_os_upgrade_builder (struct maction_context *);
struct maction *backend_maction_rx_sysex_builder (struct maction_context *);
struct maction *backend_maction_tx_sysex_builder (struct maction_context *);
struct maction *microbrute_maction_conf_builder (struct maction_context *);
struct maction *microbrute_maction_cal_builder (struct maction_context *);
struct maction *microfreak_maction_defrag_builder (struct maction_context *);

void
regma_register ()
{
  g_slist_fill (&mactions, microbrute_maction_conf_builder,
		microbrute_maction_cal_builder,
		microfreak_maction_defrag_builder,
		maction_separator_builder,
		backend_maction_rx_sysex_builder,
		backend_maction_tx_sysex_builder,
		maction_separator_builder,
		backend_maction_os_upgrade_builder,
		maction_separator_builder, autosampler_maction_builder, NULL);
}

void
regma_unregister ()
{
  g_slist_free (mactions);
  mactions = NULL;
}
