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

struct maction *ma_autosampler_builder (struct maction_context *);
struct maction *ma_backend_os_upgrade_builder (struct maction_context *);
struct maction *ma_backend_rx_sysex_builder (struct maction_context *);
struct maction *ma_backend_tx_sysex_builder (struct maction_context *);
struct maction *ma_elektron_ram_purge_builder (struct maction_context *);
struct maction *ma_microbrute_conf_builder (struct maction_context *);
struct maction *ma_microbrute_cal_builder (struct maction_context *);
struct maction *ma_microfreak_defrag_builder (struct maction_context *);

void
regma_register ()
{
  gslist_fill (&mactions, ma_elektron_ram_purge_builder,
	       ma_microbrute_conf_builder,
	       ma_microbrute_cal_builder,
	       ma_microfreak_defrag_builder,
	       ma_backend_rx_sysex_builder,
	       ma_backend_tx_sysex_builder,
	       ma_backend_os_upgrade_builder, ma_autosampler_builder, NULL);
}

void
regma_unregister ()
{
  g_slist_free (g_steal_pointer (&mactions));
}
