<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception -->
# Contributing to Forge

Thanks for wanting to help! Forge is a build system for C projects: your
build script (`build.c`) is real C, compiled to an executable by the
host tool `forge`, and dispatched by `function_<name>`.

This file defines how to contribute and - importantly - how every
contribution is licensed. Please read it before your first PR.

## Reporting issues

- **Bug reports** should include: forge version (`forge --version`),
  platform, the failing `build.c`/`build.conf`, reproduction steps, and
  the output of `./make test` if applicable.
- **Feature requests**: describe the use case, not just the feature.

## Building and testing

```sh
./bootstrap.sh   # from zero: host forge + runtime library + full build + tests
./make test      # run the test suite
./make gen_deb   # build a .deb package (Linux)
./make install /usr/local   # install to a prefix (Linux)
```

Two notes every contributor needs:

- `make` is a self-contained executable: changes to the public headers
  (`lib/include/*.h`) or the runtime library take effect only after a
  full `./bootstrap.sh` (the headers are snapshotted into the bootstrap
  prefix). Incremental `forge .` does not refresh them.
- Public API names are part of the product (see Code style); renaming
  them is a breaking change - discuss before doing it.

## Code style

- **C23**; keep comments truthful and current with the code. Prefer
  English comments; user-visible messages must be English.
- **Naming conventions**:
  - DSL configuration actions: `set_*` (replace a value) and `add_*`
    (append to a set); `compile()` executes.
  - Platform layer: `os_*`; string vectors: `strv_*`; runtime library
    internals: `forge_*`.
  - Do **not** use `__`-prefixed identifiers - double-underscore is
    reserved for the implementation by the C standard (this repository
    already migrated its internals from `__x` to `_x`).
  - strv_t fields are `count` / `capacity`; collections that track a
    length use a `*_count` field.
- Error messages: `forge: ...` prefix, end with `\n`, explain both what
  went wrong and the likely fix.
- Every code change must keep `./make test` green.

## Writing tests

Two families, with different rules:

- **Unit tests** (`tests/*.c`): pure logic, in-process, zero OS calls,
  zero subprocess spawn. See `tests/test_null.c`, `tests/test_linker.c`.
- **Scenario tests** (`tests/edge/`): end-to-end behavior - they build
  tiny projects and spawn `forge`/`./make` through `tests_common.h`.
  Use `edge_init_conf()` for a valid project skeleton; each scenario
  gets its own temporary directory under `test/edge_*`.

Test file header is a contract parsed by `parse_test_c`:

```c
// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
```

Only leading `//` lines are parsed - the first non-`//` line ends the
header (see `tests/test_os_remove.c`).

**Isolation rules** (these are hard requirements): tests may only read
and write gitignored areas (`test/edge_*`, `build/`, `test/`); they must
never install, never touch `/usr`, never modify the environment of the
host. `/make test` is also run by users - keep it light, quick, and safe.

## License and contribution acknowledgment

Forge's source code, documentation, test suite, and examples are
licensed under the **GNU GPL version 3 or later**, including an **additional permission under GPLv3+ §7** stated in
`LICENSE`: copyleft does not extend to build
scripts or build artifacts - you may copy portions of this software into
a `build.c` without open-sourcing the build script or its build
artifacts and without crediting the author, provided you do not
misrepresent authorship and do not claim exclusive copyright over the
copied portions.

By contributing, you agree that your contribution is licensed under the
same terms. **Every pull request must include, verbatim, in its
description:**

> I confirm that I have read and agree to the license terms, including
> all additional permissions and additional terms, as stated in
> CONTRIBUTING.md.

PRs without this acknowledgment will not be merged. Maintainers verify
it before review; partial or paraphrased versions do not count.

**Origin of contributions.** A contribution must be your original work,
or third-party material licensed under terms compatible with the terms
above (GPLv3+ with the additional permission in `LICENSE`). The
additional permission concerns portions copied from Forge itself, and
does not cover third-party material you add. Do not include third-party
code under an incompatible license (for example, GPL-2.0-only or
proprietary code).