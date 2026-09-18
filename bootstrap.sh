# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception
# Copyright (C) 2026 The Forge development team
# Additional permission under GPLv3+ §7 applies; see LICENSE.

#!/bin/sh
# bootstrap.sh - bootstrap Forge from zero (no preinstalled artifacts
# needed).
#
# Bootstrap order (resolves the chicken-and-egg problem):
#   1. Compile the host tool forge directly with the system compiler
#      (the only hand-written gcc step);
#   2. Package the first runtime library libmain.a by hand and install
#      it, together with the headers, into a local prefix
#      (build/output/); forge links against it when it
#      generates ./make;
#   3. forge compiles build.c into ./make; from here on everything is
#      left to make (make rebuilds forge / libmain.a / libforge.so,
#      and "make test" runs the test suite).
#
# All artifacts land under .gitignore-covered paths (build/, test/,
# ./make, ./.forge/). Re-runnable: the prefix is rebuilt every time,
# everything else is decided incrementally.
#
# Usage:
#   ./bootstrap.sh              # build + full test run
#   PREFIX=/usr/local ./bootstrap.sh --install   # build and install
#   The compiler defaults to build.conf's compiler_path; override with
#   CC=/path/to/gcc. This script is a POSIX sh flow; on Windows it can
#   be run under WSL/MSYS.

set -e

cd "$(dirname "$0")"

# ---- Compiler and version (single source of truth: the version key
#      in build.conf, analogous to .env) ----
CC=${CC:-$(sed -n 's/^compiler_path=//p' build.conf 2>/dev/null | head -1)}
CC=${CC:-gcc}
VER=$(sed -n 's/^version=//p' build.conf 2>/dev/null | head -1)
VER=${VER:-dev}

"$CC" --version >/dev/null 2>&1 || { echo "bootstrap: compiler unavailable: $CC" >&2; exit 1; }

echo "bootstrap: CC=$CC FORGE_VERSION=$VER"
# Header snapshot goes to <prefix>/lib/forge/include — the single
# layout shared with install/gen_deb and resolve_include_dir (see
# build.c install_to / src/forge.c). Do not re-introduce a second
# layout without updating all three.
mkdir -p build/output

# ---- 1) Host tool forge (same sources and flags as the
#         target("forge") in build.c) ----
"$CC" -std=c23 -O2 "-DFORGE_VERSION=\"$VER\"" -Iinclude -Ilib/include \
      src/forge.c src/cli.c src/parser.c src/generate.c lib/src/os/*.c \
      -o build/output/forge
echo "bootstrap: host forge -> build/output/forge"

# ---- 2) First runtime library + build-time headers ----
# Everything lands in build/output/ — make's own product area — so
# that from here on ./make (and ./make test) depends only on make's
# own artifacts. No bootstrap-* directories are ever created.
# The runtime library contains all of lib/src plus the entry point
# src/build/main.c (the target("main") in the root build.c is exactly
# this combination; the generated ./make gets its main from it. This
# hand-built copy is the bootstrap bridge for the very first ./make;
# afterwards target("main") maintains the same path incrementally).
# The forge host resolves headers from <exe_dir>/include and the
# library from <exe_dir> — both are build/output/ here.
mkdir -p build/output/include 2>/dev/null || exit 1
cp lib/include/*.h build/output/include/
tmpobj=$(mktemp -d /tmp/forge-boot.XXXXXX) || exit 1
objs=""
for f in $(find lib/src -name '*.c') src/build/main.c; do
  o="$tmpobj/$(printf '%s' "$f" | sed 's#lib/src/##; s#/#_#g' | tr -d '/').o"
  "$CC" -std=c23 -O2 -Iinclude -Ilib/include -Ilib/src -c "$f" -o "$o" || exit 1
  objs="$objs $o"
done
# shellcheck disable=SC2086 (objs is an expanded, space-separated .o list)
ar rcs build/output/libmain.a $objs
rm -rf "$tmpobj"
echo "bootstrap: runtime library -> build/output/libmain.a"

# ---- 3) forge compiles build.c into ./make (resolves build.h and
#         libmain.a from build/output/) ----
./build/output/forge .

# ---- 4) Bootstrap takes over: make rebuilds everything ----
./make
echo "bootstrap: build complete (forge / libmain.a / libforge.so)"

case "${1:---test}" in
  --no-test) ;;
  --install)
    install_prefix=${PREFIX:-/usr/local}
    ./make install "$install_prefix"
    echo "bootstrap: installed to $install_prefix"
    ;;
  *)
    ./make test
    ;;
esac