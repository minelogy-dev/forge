#!/usr/bin/env bash
# Compile every production translation unit; warnings are CI failures.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
compiler=${CC:-gcc}
flags=(-std=c23 -O1 -Wall -Wextra -Werror -Iinclude -Isrc -Ilib/include -Ilib/src)
case "${1:-warnings}" in
  warnings) ;;
  analyze) flags+=(-fanalyzer -fno-analyzer-state-merge) ;;
  *) echo "Usage: bash .github/scripts/check-c.sh [warnings|analyze]" >&2; exit 2 ;;
esac
mkdir -p build/ci
object_dir=$(mktemp -d "$PWD/build/ci/check-c.XXXXXX")
trap 'rm -rf "$object_dir"' EXIT
mapfile -d '' sources < <(git ls-files -z -- 'src/*.c' 'lib/src/*.c')
(( ${#sources[@]} > 0 ))
for source in "${sources[@]}"; do
  echo "Checking $source"
  "$compiler" "${flags[@]}" -c "$source" -o "$object_dir/${source//\//_}.o"
done
