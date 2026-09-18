<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception -->
# Forge

**Forge is a build system for C projects: the build script is a real
C program.** It is written in C and ships two toolchains - GCC and
Clang (Clang reuses the GCC-compatible argument subset). Linux is
currently the only supported platform; the architecture reserves an
`os_` layer for portability to others.

A build script is just an ordinary C source file (`build.c`). The host
tool `forge` compiles it into an executable and then runs it - the
build system compiles the build script, then runs that script to build
itself.

## Quick Start

```sh
./bootstrap.sh        # bootstrap from zero: host forge + runtime library + full build + tests
./make                # incremental build (re-running the ./make produced by "forge .")
./make test           # run the test suite
./make install /usr/local     # install to the given prefix (Linux)
./make gen_deb        # build a .deb package (Linux)
```

`./bootstrap.sh` is the only step in the whole pipeline that requires
manual work: it compiles the host tool directly with the system
compiler and hand-packages the first runtime library. From then on,
everything is handled by the bootstrapped `./make`. See the
comments at the top of `bootstrap.sh` for details.

### Build script example

A project's build script is a `build.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/* Example project: identical, verbatim, to the "Build script
   example" in the README */

#include <build.h>
function(build) {
  parse_args(builtin_args);
  target("app") {
    set_toolchain(GCC);
    add_sources_r("src");
    add_include_path("include");
    set_optimization(OPT_SPEED);
    if (compile() != 0)
      return -1;
  }
  return 0;
}
set_default(build);
default_test();
```

Put the file in your project root (together with a `build.conf`), then:

```sh
forge .        # compile build.c to generate ./make
./make         # first full build; after that, incremental via .d/.meta
```

Artifacts are output to `output/` in the project root (the default
output directory; change it with `set_output_path()`). In this example
that's `output/app`.

`build.conf` decides how forge itself generates `./make` - that is,
**which compiler is used to compile the build.c script itself** (and
which libraries make links against). It is unrelated to the toolchain
of your project's targets: which compiler a target uses is decided by
`set_toolchain()` inside each `target()` block in `build.c`. The two
do not interact:

```
compiler=GCC
compiler_path=/usr/bin/gcc
```

Configuration keys can be overridden per platform by suffixing the key
name (`compiler_windows=...`, `compiler_linux=...`, `compiler_mac_os=...`),
falling back to the bare key for the platform's default. Every setting
is injected as a macro when compiling build.c, as
`-DCONF_<KEY>=<VALUE>` (the same transport as FORGE_VERSION;
compilation goes straight through execv with no shell layer, values
are not wrapped in quotes, leading/trailing whitespace is trimmed, and
the key name is prefixed with `CONF_` as written). If build.c expects
a string value, stringify it yourself, e.g.:

```c
#define STR(tok) #tok
if (strcmp(STR(CONF_compiler), "GCC") == 0) { /* ... */ }
```

Lines starting with `#` are comments (whole-line or trailing; the rest
of the line is stripped).

`forge` is the bootstrapped host tool (`build/output/forge`).
Until it is installed system-wide, give it a prefix that contains the
headers and the runtime library and it will work on any project; you
can also install it first and then just run `forge .`:

```sh
FORGE_PREFIX=$PWD/build/bootstrap-prefix build/output/forge ~/myproject
# or: after ./make install /usr/local, run forge directly on any project
```

The generated script's executable name is set with `output_name`
(default `make`; it must be a plain file name without path
separators):

```
output_name=mybuild     # generates ./mybuild (inline comment example)
```

The full DSL surface (all `set`/`add` options, target types,
languages, optimization levels) is documented in
`lib/include/build.h`.

Every `function(build)` is a function inside the generated executable,
and can also be invoked individually as a subcommand:

```sh
./make install /usr/local    # equivalent to calling function_install(argc, argv)
./make test                  # function_test
```

## Design

**Compile the build script into a program - don't interpret it.**
`build.c` is real, compilable C source. `forge` compiles and links it;
the result is an executable that contains all of your `function()`s.
At runtime, functions are found by name (`function_<name>`) and
called, so every function is naturally a subcommand, with arguments
arriving as `argc/argv`.

**Macros collect configuration; functions execute actions.**
The `set`/`add` macros only write configuration into a `target_t`;
`compile()` does the actual work: incremental decisions, parallel
compilation, and backend selection by target type. Macros are the
declaration layer, `compile()` is the execution layer - the two are
kept separate.

**Platform differences converge in the os_ layer.**
Platform-dependent capabilities (processes, pipes, paths, time,
threads) are declared in `forge_os.h` and implemented per platform.
Command generation is split by toolchain backend (compiler / linker /
archiver). Cross-platform semantics (quoting, exit codes) follow a
single contract.

**Build artifacts**
Forge bootstraps three kinds of artifacts, all under `build/`:

| Artifact | Contents | Purpose |
|------|------|------|
| `build/output/forge` | Host tool (src/ + lib/src/os) | Compiles any build.c into a make |
| `build/output/libmain.a` / `build/shared/output/libforge.so` | Runtime library (lib/src) | Link input for make, plus its shared version |
| `./make` | Build-script object + runtime library | Runs this project's build functions |

**Tests are just small programs.**
A single `default_test()` line enables the entire test suite: it
defines a `test` task (invoked via `./make test`) that recursively
scans `tests/`, compiles each `tests/*.c` into its own standalone test
program - dependencies and compiler flags self-declared in the leading
`// Source`/`Include`/`Library`/`Link`/`Option` header entries - runs
it, and prints a summary (`forge: N tests, X passed, Y failed`),
exiting non-zero if anything failed. Adding a `tests/xxx.c` adds a
test.

## Directory structure

```
build.c          this project's build script; also a reference for the DSL
bootstrap.sh     bootstrap script: builds from zero and runs the tests
src/             host tool (forge / cli / parser / generate)
include/         host tool headers
lib/             runtime library (os / compiler / linker / archiver / ...)
tests/           test suite (// Source entries self-declare dependencies)
examples/        example projects (examples/demo)
```

## License

Forge itself, the runtime library, the documentation and the examples
are licensed under GPLv3+ - full terms in `LICENSE`, which also
contains the additional permission under GPLv3+ §7.

As an exception, you may copy parts of Forge's source code directly
into a build script and modify them as needed. A "build script" means
a file Forge uses as a project's build definition or build entry
point, whatever its name, extension or location - for example, the
default `build.c`. Solely because of the copied parts, that build
script and its build outputs are not required to be licensed or have
their source released under GPLv3+, and no attribution for the copied
parts is required. The conditions are: you must not misstate the
authorship of the copied parts, and you must not claim exclusive
copyright over them.

The code you copy in is yours to maintain. Internal symbols may
change between versions.

The generated `make` (build-script object + runtime library) may be
redistributed under GPLv3+ terms, though that is not recommended: it
binds your project to a specific Forge version and standalone
distribution is of limited value. You cannot embed Forge into an
unrelated application as a general-purpose library - the additional
permission covers only build scripts and their build outputs;
embedding the full library remains subject to GPLv3+ (see `LICENSE`).

Contributions are welcome - see `CONTRIBUTING.md` for the
contribution flow and the license-confirmation step for pull
requests.

## Not currently supported

- Windows / MSVC (the os layer and the compiler backend are stubs,
  untested and disabled on that platform; targeting it fails at link
  time)
- macOS compatibility guarantees (same: the os layer is a stub)
- Package management
- Cross-compilation matrix
- IDE project generation

These are not bugs - they are the current boundaries. Issues and
feature requests are welcome to help set priorities.