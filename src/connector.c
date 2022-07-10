/*
 *   connector.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
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

#include "backend.h"
#include "connector.h"
#include "backend/elektron.h"
#include "backend/sds.h"

gint
connector_init (struct backend *backend, gint card)
{
  int err = backend_init (backend, card);
  if (err)
    {
      return err;
    }

  err = elektron_handshake (backend);
  if (err)
    {
      err = sds_handshake (backend);
    }
  if (err)
    {
      backend_destroy (backend);
    }

  return err;
}
