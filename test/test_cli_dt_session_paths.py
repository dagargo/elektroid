"""Compile and exercise the standalone slot validator, without MIDI."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SessionPathTest(unittest.TestCase):
    def test_only_sound_slots_are_allowed(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'paths.c'
            source.write_text('''
#include "cli_dt_session.h"
int main(void) {
  const char *valid[] = {"/soundbanks/A/1", "/soundbanks/H/256", "/soundbanks/D/006"};
  const char *invalid[] = {"/", "/soundbanks/D", "/soundbanks/D/", "/soundbanks/D/0",
    "/soundbanks/D/257", "/soundbanks/I/1", "/soundbanks/a/1", "/soundbanks/D/+1",
    "/soundbanks/D/1/", "/soundbanks/D/1/.metadata", "/projects/1", "",
    "/soundbanks/D/999999999999999999999"};
  for (unsigned i = 0; i < sizeof(valid)/sizeof(*valid); i++)
    if (!cli_dt_sound_slot_path_valid(valid[i])) return 1;
  for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); i++)
    if (cli_dt_sound_slot_path_valid(invalid[i])) return 2;
  return cli_dt_sound_slot_path_valid(0) ? 3 : 0;
}
''')
            subprocess.run([compiler, '-Wall', '-Werror', '-I',
                            str(Path(__file__).resolve().parents[1] / 'src'),
                            str(source), '-o', str(root / 'paths')], check=True)
            subprocess.run([str(root / 'paths')], check=True)
