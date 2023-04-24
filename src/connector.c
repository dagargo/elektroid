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
#include "connectors/elektron.h"
#include "connectors/microbrute.h"
#include "connectors/cz.h"
#include "connectors/sds.h"
#include "connectors/efactor.h"
#include "connectors/phatty.h"
#include "connectors/summit.h"
#include "connectors/default.h"

struct connector
{
  gint (*handshake) (struct backend * backend);
  const gchar *name;
  //If the backend device name matches this regex, the handshake will be run before than the connectors that didn't match.
  const gchar *regex;
};

static const struct connector CONNECTOR_ELEKTRON = {
  .handshake = elektron_handshake,
  .name = "elektron",
  .regex = ".*Elektron.*"
};

static const struct connector CONNECTOR_MICROBRUTE = {
  .handshake = microbrute_handshake,
  .name = MICROBRUTE_NAME,
  .regex = ".*MicroBrute.*"
};

static const struct connector CONNECTOR_CZ = {
  .handshake = cz_handshake,
  .name = "cz",
  .regex = NULL
};

static const struct connector CONNECTOR_SDS = {
  .handshake = sds_handshake,
  .name = "sds",
  .regex = NULL
};

static const struct connector CONNECTOR_EFACTOR = {
  .handshake = efactor_handshake,
  .name = "efactor",
  .regex = ".*Factor Pedal.*"
};

static const struct connector CONNECTOR_PHATTY = {
  .handshake = phatty_handshake,
  .name = "phatty",
  .regex = ".*Phatty.*"
};

static const struct connector CONNECTOR_SUMMIT = {
  .handshake = summit_handshake,
  .name = "summit",
  .regex = ".*(Peak|Summit).*",
};

static const struct connector CONNECTOR_DEFAULT = {
  .handshake = default_handshake,
  .name = "default",
  .regex = NULL
};

static const struct connector *CONNECTORS[] = {
  &CONNECTOR_ELEKTRON, &CONNECTOR_MICROBRUTE, &CONNECTOR_PHATTY,
  &CONNECTOR_SUMMIT, &CONNECTOR_CZ, &CONNECTOR_SDS, &CONNECTOR_EFACTOR,
  &CONNECTOR_DEFAULT, NULL
};

// A handshake function might return these values:
// 0, the device matches the connector.
// -ENODEV, the device does not match the connector but we can continue with the next connector.
// Other negative errors are allowed but we will not continue with the remaining connectors.

gint
connector_init_backend (struct backend *backend,
			struct backend_system_device *device,
			const gchar * conn_name,
			struct sysex_transfer *sysex_transfer)
{
  gint err;
  GSList *list = NULL, *iterator;
  gboolean active = TRUE;
  const struct connector **connector;

  err = backend_init (backend, device->id);
  if (err)
    {
      return err;
    }

  connector = CONNECTORS;
  while (*connector)
    {
      if ((*connector)->regex)
	{
	  GRegex *regex = g_regex_new ((*connector)->regex, G_REGEX_CASELESS,
				       0, NULL);
	  if (g_regex_match (regex, device->name, 0, NULL))
	    {
	      debug_print (1, "Connector %s matches the device\n",
			   (*connector)->name);
	      list = g_slist_prepend (list, (void *) *connector);
	    }
	  else
	    {
	      list = g_slist_append (list, (void *) *connector);
	    }
	  g_regex_unref (regex);
	}
      else
	{
	  list = g_slist_append (list, (void *) *connector);
	}
      connector++;
    }

  err = -ENODEV;
  for (iterator = list; iterator; iterator = iterator->next)
    {
      const struct connector *c = iterator->data;

      if (sysex_transfer)
	{
	  g_mutex_lock (&sysex_transfer->mutex);
	  active = sysex_transfer->active;
	  g_mutex_unlock (&sysex_transfer->mutex);
	}

      if (!active)
	{
	  err = -ECANCELED;
	  goto end;
	}

      if (conn_name)
	{
	  if (!strcmp (conn_name, c->name))
	    {
	      debug_print (1, "Testing %s connector...\n", c->name);
	      err = c->handshake (backend);
	      if (!err)
		{
		  debug_print (1, "Using %s connector...\n", c->name);
		  backend->conn_name = c->name;
		}
	      goto end;
	    }
	}
      else
	{
	  debug_print (1, "Testing %s connector...\n", c->name);
	  err = c->handshake (backend);
	  if (err && err != -ENODEV)
	    {
	      goto end;
	    }

	  if (!err)
	    {
	      debug_print (1, "Using %s connector...\n", c->name);
	      backend->conn_name = c->name;
	      goto end;
	    }
	}
    }

  error_print ("No device recognized\n");

end:
  g_slist_free (list);
  if (err)
    {
      backend_destroy (backend);
    }
  return err;
}
