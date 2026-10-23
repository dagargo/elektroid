/* Target validation for the experimental Digitakt session protocol. */
#ifndef CLI_DT_SESSION_H
#define CLI_DT_SESSION_H

#include <string.h>

static inline int
cli_dt_sound_slot_path_valid (const char *path)
{
  const char *slot;
  unsigned int id = 0;
  const char *prefix = "/soundbanks/";

  if (!path || strncmp (path, prefix, strlen (prefix)))
    return 0;
  slot = path + strlen (prefix);
  if (slot[0] < 'A' || slot[0] > 'H' || slot[1] != '/')
    return 0;
  slot += 2;
  if (!*slot)
    return 0;
  while (*slot)
    {
      if (*slot < '0' || *slot > '9')
        return 0;
      id = id * 10 + (*slot++ - '0');
      if (id > 256)
        return 0;
    }
  return id >= 1;
}

#endif
