#!/bin/sh
set -eu
command -v python3 >/dev/null 2>&1 || exit 77
exec python3 -m unittest discover -s "${srcdir:-.}" -p test_cli_dt_session_paths.py
