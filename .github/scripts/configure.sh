#!/usr/bin/env bash
# Configure the local build entry point for CI or an equivalent local run.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
compiler_path=$(command -v "${CC:-gcc}")
case "$(basename "$compiler_path")" in
  gcc*) backend=GCC ;;
  clang*) backend=CLANG ;;
  *) echo "CC must select GCC or Clang" >&2; exit 2 ;;
esac
sed -i '/^compiler=/d; /^compiler_path=/d; /^sanitize=/d' build.conf
printf '\ncompiler=%s\ncompiler_path=%s\nsanitize=1\n' "$backend" "$compiler_path" >> build.conf
"$compiler_path" --version
