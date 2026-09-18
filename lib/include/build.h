/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file build.h
 * @brief The libforge build DSL: macros and runtime interfaces used by
 *        build scripts (build.c).
 *
 * A build script describes the build flow through this header:
 * - function(x) defines a build task named x (the artifact runtime
 *   invokes it by name);
 * - target(x) opens a target block, inside which the set_* / add_*
 *   macro families configure the target, and compile() finally runs
 *   the compile pass and link pass;
 * - set_default(x) registers task x as the default task (invoked when
 *   the runtime is called with no arguments).
 *
 * Platform macros:
 * - LINUX / WIN32 / MAC_OS are auto-detected at compile time; build
 *   scripts can use them directly in #if; exactly one of the three is
 *   1;
 * - UNIX is 1 for Unix-like systems (FreeBSD/Linux/macOS etc.), and 0
 *   on Windows; build scripts can use it to pick between Unix/Windows
 *   OS source file lists;
 * - the remaining DSL macros rely on the implicit variable _target
 *   within the target block scope (introduced by target(x)) and are
 *   only valid inside a target block.
 *
 * The runtime interfaces (functions prefixed with __) are implemented
 * by the libforge runtime library; build scripts generally do not need
 * to call them directly.
 */
#ifndef FORGE_BUILD
#define FORGE_BUILD

#include "forge_os.h"
#include "forge_type.h"
#include "forge_def.h" /* source of the GCC/CLANG/MSVC etc. constants */
                        /* used in DSL scripts */
#include <stdlib.h>

#if defined(__unix__) || defined(_unix) || defined(unix)
/** @def UNIX Unix-like target-platform marker */
#define UNIX 1
#else
#define UNIX 0
#endif

#if defined(__linux__) || defined(_linux) || defined(linux)
/** @def LINUX Linux target-platform marker */
#define LINUX 1
#else
#define LINUX 0
#endif

#if defined(_WIN32) || defined(_WIN64)
/** @def WIN Windows target-platform marker */
#define WIN 1
#else
#define WIN 0
#endif

#if defined(__APPLE__) || defined(__MACH__)
/** @def MAC_OS macOS target-platform marker */
#define MAC_OS 1
#else
#define MAC_OS 0
#endif

#if !defined(__cplusplus) &&                                                   \
    (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
/** @def nullptr fallback below C23: nullptr is a C23 keyword; under old
 * standards/compilers, NULL is substituted */
#define nullptr NULL
#endif

/* ------------------------ test DSL default macros ------------------------ */

/**
 * @def LANGUAGE
 * @brief default_test()'s default language configuration
 *        (set_language(C)).
 *
 * Both are #ifndef-guarded default macros: after default_test() is
 * expanded, LANGUAGE; TOOLCHAIN; are invoked in turn inside each target
 * block. A build script can #define overrides first (e.g. `#define
 * LANGUAGE set_language(CPP)`), and whatever default_test() references
 * is the overriding definition. The default combination matches the
 * plan.md example (the compiler path is written directly via
 * set(TARGET_COMPILER_PATH,1,p)).
 */
#ifndef LANGUAGE
#define LANGUAGE set_language(C)
#endif

/**
 * @def TOOLCHAIN
 * @brief default_test()'s default toolchain configuration
 *        (set_toolchain(GCC)).
 * See LANGUAGE's override rules.
 */
#ifndef TOOLCHAIN
#define TOOLCHAIN set_toolchain(GCC)
#endif

/* ------------- argument parsing facilities (-j / -v) ------------- */

/**
 * @def OPTS_COUNT
 * @brief Number of slots in the option table (256).
 *
 * Binding convention with forge_parse_args(): short options are
 * indexed by character code (0-127, e.g. 'j'=106); long options
 * without a short form go into high slots above 128 (e.g. [128]), so
 * the short-option dispatch branch `opts[(unsigned char)ch]` cannot
 * hit them by mistake.
 */
#define OPTS_COUNT 256

/**
 * @brief Command-line option table entry.
 *
 * The input/output table of forge_parse_args(); slots are indexed by
 * key (filled inside the parse_args macro by the OPT / builtin_args
 * macros with designated-initializer syntax):
 * - `long_name == NULL` means the slot is unused (other fields are
 *   treated as 0);
 * - when `takes_arg` is non-zero, the parsed argument is written to
 *   `arg` (pointing into argv, not written back; with `--jobs=8`, the
 *   '=' in argv is rewritten to '\0' as the split);
 * - when `takes_arg` is 0, `flag_store` is incremented on every hit
 *   (the set/count boolean ambiguity is the user's convention).
 */
typedef struct {
  const char *long_name; /**< long option name (without --); NULL means */
                         /**< the slot is unused */
  int takes_arg;         /**< whether the option takes an argument */
  int flag_store;        /**< number of hits (count/boolean for no-arg */
                         /**< options) */
  int argc;              /**< argument count */
  char **argv;           /**< argument values of argument-taking options
                             (pointing into argv, NULL-terminated); the
                             pointers point directly into the entry
                             point's argv; no free needed */
} opt_t;

/**
 * @brief Parse command-line options (same semantics as the forge
 *        tool's parse_args).
 *
 * Rules:
 * - short options are indexed by character code
 *   `opts[(unsigned char)ch]`; no-arg short options can be clustered
 *   (`-vj` sets them in sequence); argument-taking short options
 *   support both attached `-j8` and split `-j 8` forms;
 * - long options support `--jobs=8` and `--jobs 8` (when the next
 *   argv starts with '-', it is judged as a missing argument); long
 *   option names support prefix matching, exact match wins, and
 *   multi-candidate ambiguity is an error;
 * - unknown long option, ambiguity, missing argument, and other errors
 *   exit(EXIT_FAILURE) directly;
 * - `--` ends option parsing; the first positional argument stops
 *   parsing and its index is returned.
 *
 * @param[in] argc       length of argv.
 * @param[in] argv       the argument array (rewritten in place: '='
 *                       is truncated).
 * @param[in,out] opts   the option table (must be zero-initialized to
 *                       OPTS_COUNT size).
 * @param[in] opts_count number of slots in the option table.
 * @return the index of the first positional argument (argc when none).
 */
int forge_parse_args(int argc, char **argv, opt_t *opts, int opts_count);

/**
 * @def OPT(key, long_name, takes_arg, store, arg)
 * @brief Option entry for forge_parse_args() (expands to a designated
 *        initializer).
 *
 * Used at forge_parse_args()'s argument position, one per entry:

 * @code
 *   OPT('f', "foo", 0, 0, NULL),
 *   OPT(128, "feature", 0, 0, NULL),   // no short form; key > 128
 * @endcode
 *
 * - key is the subscript into the opts array: short options must use
 *   their character code (`'f'`); long options without a short form go
 *   into high slots above 128 (e.g. `[128]`), so the short-option
 *   branch `opts[(unsigned char)ch]` cannot hit them by character
 *   code;
 * - store is the variable set after parsing: when the entry is hit,
 *   flag_store is incremented, and the caller reads it after
 *   forge_parse_args() (e.g. `opts['f'].flag_store`);
 * - when takes_arg is non-zero, arg's initial value is usually NULL
 *   and is written by forge_parse_args().
 */
#define OPT(key, long_name, takes_arg, store, arg)                             \
  [key] = {long_name, takes_arg, store, arg}

/**
 * @def builtin_args
 * @brief Default entries for forge_parse_args(): preset -j/--jobs and
 *        -v/--verbose.
 *
 * Spliced as an object-like macro into forge_parse_args()'s
 * initializer list:
 * - `['j'] = {"jobs", 1, 0, NULL}`: N-way parallel compile (default 1);
 * - `['v'] = {"verbose", 0, 0, NULL}`: verbose output.
 * Expands to two designated-initializer entries without trailing
 * commas (the separating commas between entries come from
 * forge_parse_args()'s argument commas); can be mixed freely in any
 * order with other OPT(...) entries.
 */
#define builtin_args                                                           \
  ['j'] = {"jobs", 1, 0, 0, NULL}, ['v'] = {"verbose", 0, 0, 0, NULL},        \
  ['q'] = {"quiet", 0, 0, 0, NULL}

/**
 * @def forge_parse_args(...)
 * @brief Argument parsing entry inside function(x): sets up the opts
 *        array and parses -j/-v.
 *
 * Get -j/-v (and custom options) in one line:
 * @code
 *   function(build) {
 *     parse_args(builtin_args);
 *     forge_parse_args(builtin_args, OPT(128, "foo", 0, 0, NULL));
 *     parse_args();       // empty opts
 *     ...
 *   }
 * @endcode
 *
 * Implementation notes:
 * - opts is initialized as `{ {0}, __VA_ARGS__ }`: builtin_args and
 *   each OPT(...) expand to one designated-initializer entry (no
 *   trailing comma; the separating commas between entries come from
 *   the macro-call argument commas), so no combination or ordering of
 *   arguments ever produces a double comma; a zero-argument call
 *   degenerates to `{ {0}, }`, a legal trailing comma - all of the
 *   above is valid C99, no C23 / `__VA_OPT__` / constexpr needed;
 *   `{0}` is opts[0]'s braced scalar initialization, avoiding
 *   -Wmissing-braces;
 * - the trailing epilogue unconditionally reads `opts['v']` and
 *   `opts['j']`: with a purely empty opts the slots are
 *   zero-initialized, equivalent to `os_set_verbose(0);
 *   os_set_jobs(1);`, harmless;
 * - `opts['j'].arg` is filled by forge_parse_args() (`-j8`/`-j 8`/
 *   `--jobs=8` all work), and after atoi it is clamped to >= 1 via
 *   os_set_jobs();
 * - unknown/ambiguous/missing-argument options make
 *   forge_parse_args() exit(EXIT_FAILURE);
 * - the macro depends on `argc` / `argv` in the enclosing scope (i.e.
 *   function(x)'s parameters) and is only valid inside function(x)'s
 *   body.
 * - when used in if/for/while, always wrap it in {}.
 *
 * @param 0 or more OPT(...) / builtin_args entries.
 */
#define parse_args(...)                                                            \
  opt_t _opts[OPTS_COUNT] = {{0}, __VA_ARGS__};                                 \
  do {                                                                         \
    forge_parse_args(argc, argv, _opts, OPTS_COUNT);                                \
    /* Three states: -v -> 1; -q -> -1; neither -> 0. Giving both -q and       \
       -v is undefined behavior; the outcome follows this expression's         \
       evaluation order naturally; no promise is made. */                      \
    os_set_verbose(_opts['q'].flag_store ? -1                              \
                   : _opts['v'].flag_store ? 1 : 0);                       \
    os_set_jobs((_opts['j'].argv && *_opts['j'].argv) ? atoi(_opts['j'].argv[_opts['j'].argc-1])  \
                                                      : 1);                    \
  } while (0)

/**
 * @def function(x)
 * @brief Define a build task named x.
 *
 * Expands to the task function definition:
 *   int function_x(int argc, char **argv)
 * Usage is function(x) { ... }. The artifact runtime (see
 * src/build/main.c) looks up and calls this symbol as
 * "function_<name>"; argv are the arguments passed by the runtime.
 */
#define function(x) int function_##x(int argc, char **argv)

/**
 * @def target(x)
 * @brief Open a target block named x.
 *
 * Expands to a for loop: new_target(x) becomes the implicit variable
 * _target in scope, the block body runs once, and at the loop's update
 * step free_target_contents frees everything (the full release
 * includes the shell itself). Inside the block, the set_* / add_* /
 * compile() macro families configure and compile _target. A return
 * inside the block ends the build early (leaking one target_t shell,
 * which is fine when the task function is about to exit entirely); to
 * avoid the leak, jump to the end of the block with continue instead
 * of break.
 *
 * Usage:
 *   target("myapp") { ... }
 */
#define target(x)                                                              \
  for (target_t *_target = new_target(x); _target != nullptr;                \
       free_target_contents(_target), _target = nullptr)

/**
 * @def set_default(x)
 * @brief Make the task named x the default task.
 *
 * Expands to a definition of function_default(int argc, char **argv)
 * that forwards to function_x(argc, argv). The artifact runtime calls
 * function_default when invoked with no arguments.
 */
#define set_default(x)                                                         \
  int function_default(int argc, char **argv) {                                \
    return function_##x(argc, argv);                                           \
  }

// operation list
typedef enum {
  TARGET_TYPE,
  TARGET_LANGUAGE,
  TARGET_ARCHIVER,
  TARGET_ARCHIVER_PATH,
  TARGET_ASSEMBLER,
  TARGET_ASSEMBLER_PATH,
  TARGET_COMPILER,
  TARGET_COMPILER_PATH,
  TARGET_LINKER,
  TARGET_LINKER_PATH,
  TARGET_SYMBOL_LISTER,
  TARGET_SYMBOL_LISTER_PATH,
  TARGET_VISIBILITY,
  TARGET_OUTPUT_PATH,
  TARGET_OUTPUT_FINAL_PATH,
  TARGET_OUTPUT_INTERMEDIATE_PATH,
  TARGET_LTO,
  TARGET_SOURCES,
  TARGET_SOURCE_PATHS,
  TARGET_SOURCE_PATHS_R,
  TARGET_INCLUDE_PATHS,
  TARGET_LIB_PATHS,
  TARGET_LINK_LIBS,
  TARGET_LIBRARIES,
  TARGET_EXPORT_SYMBOL,
  ARCHIVER_DETERMINISTIC,
  ARCHIVER_VERBOSE,
  ARCHIVER_OPTIONS,
  ASSEMBLER_DEBUG,
  ASSEMBLER_SYNTAX_INTEL,
  ASSEMBLER_NO_EXECSTACK,
  ASSEMBLER_FATAL_WARNINGS,
  ASSEMBLER_STATISTICS,
  ASSEMBLER_OPTIONS,
  COMPILER_OPTIMIZATION,
  COMPILER_STANDARD,
  COMPILER_ADD_DEFINES,
  COMPILER_DEBUG,
  COMPILER_PIC,
  COMPILER_PIE,
  COMPILER_LTO,
  COMPILER_OPTIONS,
  LINKER_LIB_PATHS,
  LINKER_LIBS,
  LINKER_LTO,
  LINKER_SHARED,
  LINKER_STATIC_CRT,
  LINKER_STRIP,
  LINKER_GC_SECTIONS,
  LINKER_MAP_FILE,
  LINKER_OPTIONS,
  LINKER_EXPORT_SYMBOL,
  SYMBOL_LISTER_DEMANGLE,
  SYMBOL_LISTER_NUMERIC_SORT,
  SYMBOL_LISTER_UNDEF_ONLY,
  SYMBOL_LISTER_DYNAMIC,
  SYMBOL_LISTER_FORMAT,
  SYMBOL_LISTER_OPTIONS,
} forge_option_t;
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)
#define LOCATION     __FILE__ ":" TOSTRING(__LINE__)
#define set(opt, count, ...)   _set(_target, LOCATION, opt, count, __VA_ARGS__)
#define add(opt, count, ...)   _add(_target, LOCATION, opt, count, __VA_ARGS__)
int _set(target_t *target, const char* location, forge_option_t opt, int count, ...);
int _add(target_t *target, const char* location, forge_option_t opt, int count, ...);

/* ---------------- typed common wrappers (15) ----------------
 *
 * Layered contract: any modification to a target's configuration may
 * only go through set / add (the _set/_add switch is the single write
 * point). These macros are thin shells forwarding to the typed
 * internal functions _set_* / _add_*; those functions can be
 * arbitrarily complex (loops/conditionals/helper calls all allowed);
 * the only hard constraint is that every modification to the target's
 * configuration must go through _set / _add - direct field writes are
 * forbidden. All other options (e.g. TARGET_LTO,
 * LINKER_EXPORT_SYMBOL, the toolchain pieces, compiler path, etc.) are
 * written directly with set()/add().
 *
 * Compiler path example: set(TARGET_COMPILER_PATH, 1, "/usr/bin/gcc").
 */
#define set_toolchain(c)         _set_toolchain(_target, (c))
#define set_language(l)          _set_language(_target, (l))
#define set_type(t)              _set_type(_target, (t))
#define set_output_path(p)       _set_output_path(_target, (p))
#define set_output_final_path(p) _set_output_final_path(_target, (p))
#define set_output_intermediate_path(p) _set_output_intermediate_path(_target, (p))
#define set_optimization(o)      _set_optimization(_target, (o))
#define set_standard(s)          _set_standard(_target, (s))
#define set_visibility(v)        _set_visibility(_target, (v))
#define add_sources(p)           _add_sources(_target, (p))
#define add_sources_r(p)         _add_sources_r(_target, (p))
#define add_include_path(p)      _add_include_path(_target, (p))
#define add_link_lib(n)               _add_lib(_target, (n))
/* add_define(name, value): appends the macro definition `name=value`
   to the target's compile parameters (rendered as -Dname=value).
   value is passed through verbatim as a token; no quoting is added:
   - string values are wrapped explicitly by the caller:
     add_define("APP_NAME", "\"forge\"");
   - numbers/enums are passed directly: add_define("PI", "3.14")
     means #define PI 3.14;
   bare values injected via -DCONF_* from build.conf are first
   STR()'d and then quote-wrapped. */
#define add_define(name, value)  _add_define(_target, (name), (value))
#define add_option(o)            _add_option(_target, (o))

/* Typed internal functions: used only by build.c and the test
   runtime (parse_test_c); return 0 on success / -1 on failure (OOM
   etc.). parse_test_c's extension-style paths (lib/src-relative) enter
   through _add_sources. */
int _set_toolchain(target_t *t, compiler_type_t c);
int _set_language(target_t *t, target_language_t l);
int _set_type(target_t *t, file_type_t v);
int _set_output_path(target_t *t, const char *p);
int _set_output_final_path(target_t *t, const char *p);
int _set_output_intermediate_path(target_t *t, const char *p);
int _set_optimization(target_t *t, opt_level_t o);
int _set_standard(target_t *t, const char *s);
int _set_visibility(target_t *t, int v);
int _add_sources(target_t *t, const char *p);
int _add_sources_r(target_t *t, const char *p);
int _add_include_path(target_t *t, const char *p);
int _add_lib(target_t *t, const char *n);
int _add_define(target_t *t, const char *name, const char *value);
int _add_option(target_t *t, const char *o);
int _add_lib_path(target_t *t, const char *p);
int _add_link_lib(target_t *t, const char *n);

/**
 * @def add_options_for(opt, target)
 * @brief Add compile options for one specific target file only.
 *
 * Expands to _add_options_for(_target, target, opt): when the source
 * file named target joins this target's compile, opt is attached as
 * its dedicated option.
 */
#define add_options_for(opt, target) _add_options_for(_target, target, opt)

/**
 * @def compile()
 * @brief Run the target's compile and link.
 *
 * Expands to _compile(_target, argc, argv): collects source files
 * according to the current _target configuration, generates compile
 * commands, executes them one by one, and completes linking per
 * target_type, with artifacts output to output_path. Must be placed
 * inside a target(x) block (depends on _target and the task
 * function's argc/argv). The return value reports whether this compile
 * succeeded (0 success, -1 failure).
 */
#define compile() _compile(_target, argc, argv)

/* ------------------------ test DSL ------------------------ */

/**
 * @def TEST(x)
 * @brief Test assertion macro: prints a failure line to stderr and
 *        exits when the condition does not hold.
 *
 * For test .c files (programs compiled and run via default_test()):
 * `TEST(0 == atoi("0"))` and the like. When x is not true, prints
 * `FAILED: <file>:<line>: <expression>` to stderr and exit(1); when
 * true, no output at all. Subsequent assertions do not run after a
 * failure (failure exits immediately); default_test() counts
 * passed / failed from the child process's exit code.
 *
 * Depends on stdio.h and stdlib.h, both provided via forge_os.h /
 * stdlib.h already included by build.h; no extra #include needed.
 */
#define TEST(x)                                                                \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAILED: %s:%d: %s\n", __FILE__, __LINE__, #x);          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/**
 * @def PASS()
 * @brief Test pass marker: prints `PASSED: <file>` to stdout.
 *
 * For test .c files to call after all TEST assertions (usually at the
 * end of main()). The output is passed through via os_execute_raw and
 * is always visible (independent of -v).
 */
#define PASS() printf("PASSED: %s\n", __FILE__)

/**
 * @def each_file(dir)
 * @brief Recursively walk all regular files under a directory; the
 *        loop body runs once per file.
 *
 * Expands to nested double-for macros (macro black magic in the same
 * style as target()):
 * - _list_files(dir) recursively collects all regular files under dir
 *   (including subdirectories), sorted with strcmp for determinism,
 *   returning a NULL-terminated array of full relative paths;
 * - the inner loop provides the implicit variable _file (char*, the
 *   full relative path); the body runs once per file;
 * - both continue / break are safe: continue takes the inner single
 *   iteration's normal end path, and the outer update frees the
 *   current file string one by one and advances to the next; break is
 *   equally safe. The array itself is freed in the outer condition
 *   when the list is exhausted; a return inside the block leaks the
 *   file list (fine when the task function is about to exit
 *   entirely).
 *
 * Usage:
 *   each_file("tests") { printf("%s\n", _file); }
 *
 * Note: contrary to intuition, at list exhaustion the freed pointer
 * is the base pointer _each_base (the outer cursor has already
 * advanced while freeing strings element by element; the advanced
 * cursor itself must not be freed).
 */
#define each_file(dir)                                                         \
  for (char **_each_files = _list_files(dir), **_each_base = _each_files;  \
       _each_base ? (_each_files[0] ? 1 : (free(_each_base), 0)) : 0;       \
       free(*_each_files), _each_files++)                                    \
    for (char *_file = *_each_files; _file; _file = NULL)

/**
 * @def default_test()
 * @brief Bring in the default test suite: defines a task named test in
 *        the build script that scans tests/ and compiles and runs each
 *        file.
 *
 * "default" in the name means "the build script ships a ready-to-use
 * default test tooling out of the box", a different concept from
 * set_default() (which makes some task the no-argument run entry).
 *
 * Expands to `int function_test(int argc, char **argv)`, invoked by
 * the artifact runtime as `./build test` (Windows: `./build.exe
 * test`). Flow:
 * - the first line embeds parse_args(builtin_args): `-v` prints the
 *   compile pass's full commands (the test tool's own output always
 *   passes through, independent of -v); `-j N` compiles multiple
 *   source files of one test in parallel; remaining positional
 *   arguments are ignored;
 * - each_file("tests") scans recursively, each test file entering a
 *   target(_file) block: LANGUAGE / TOOLCHAIN (or script-predefined
 *   overrides) configure the target, and parse_test_c(_target,
 *   _file) completes source files / -I / -L / -l / end-of-command
 *   arguments from the file's header `// Source|Include|Library|Link|
 *   Option` entries; when it returns NULL (extension mismatch, read
 *   failure, entry failure), the file is skipped and the target shell
 *   is released by target();
 * - set_output_path("build") and set_output_final_path("test") fix
 *   the layout: intermediates under build/opt/ (incremental machinery
 *   reuses _compile's .d/.meta), the final executable under test/;
 *   the two "test" literals must stay consistent with parse_test_c's
 *   return-path join;
 * - on compile() failure (compile/link error), prints
 *   `forge: failed to build test <file>` to stderr and counts
 *   _build_failed - the compile error details have already been
 *   emitted by the compiler; the test still counts in the total and
 *   the failed column;
 * - the executable runs via os_execute_raw pass-through: the test
 *   program's FAILED (stderr) and PASSED (stdout) output is always
 *   visible; exit code 0 counts as passed, non-zero as failed;
 * - at the end prints the summary line `forge: <total> tests,
 *   <passed> passed, <failed> failed` (total and the failed column
 *   both include compile failures: complete in one place); the return
 *   code is 1 when any run or compile failed, otherwise 0 (so a CI
 *   run cannot exit 0 when test compilation broke). If tests/ does
 *   not exist or is empty, prints 0 tests and returns 0.
 *
 * Usage:
 *   default_test();            // generate with the default LANGUAGE/TOOLCHAIN
 *
 * May appear only once per build script (default_test expands to the
 * fixed name function_test; duplicate definitions conflict).
 */
#define default_test()                                                         \
  int function_test(int argc, char **argv) {                                   \
    parse_args(builtin_args);                                                  \
    /* The test task is quiet by default: without explicit -q/-v, only         \
       PASSED/FAILED is printed; all compile noise is hidden. An explicit      \
       -v restores full output. */                                             \
    if (!_opts['q'].flag_store && !_opts['v'].flag_store)                    \
      os_set_verbose(-1);                                                      \
    int _passed = 0, _failed = 0, _build_failed = 0;                        \
    each_file("tests") {                                                       \
      target(_file) {                                                         \
        LANGUAGE;                                                              \
        TOOLCHAIN;                                                             \
        char *_tool = parse_test_c(_target, _file);                         \
        if (!_tool)                                                           \
          continue; /* skipped/parse failed: no further configuration; */      \
                    /* the target shell is released by target() */             \
        set_output_path("build");                                              \
        set_output_final_path(                                                \
            "test"); /* "test" literal kept in sync with parse_test_c */       \
        if (compile() != 0) {                                                  \
          fprintf(stderr, "forge: failed to build test %s\n", _file);         \
          _build_failed++;                                                    \
          free(_tool);                                                        \
          continue;                                                            \
        }                                                                      \
        char *_argv[2] = {_tool, NULL};                                      \
        int _st = os_execute_raw(_tool, _argv, NULL);                             \
        if (_st == 0)                                                         \
          _passed++;                                                          \
        else {                                                                 \
          /* Crash/exec failure is the most common "failure with no output":   \
             the assert macro has no time to print FAILED, so mark it          \
             explicitly here; never silently counted. */                       \
          if (_st == 127)                                                     \
            fprintf(stderr, "forge: cannot run %s (exec failed)\n", _tool);   \
          else if (_st > 128)                                                 \
            fprintf(stderr, "forge: %s crashed (signal %d, exit %d)\n",        \
                    _tool, _st - 128, _st);                                 \
          _failed++;                                                          \
        }                                                                      \
        free(_tool);                                                          \
      }                                                                        \
    }                                                                          \
    printf("forge: %d tests, %d passed, %d failed\n"                           \
      , _passed + _failed + _build_failed,                                  \
           _passed, _failed + _build_failed); /* failed includes compile       \
       failures; one place tells all, no need to split the three forms. */     \
    return (_failed || _build_failed) ? 1 : 0;                               \
  }

/**
 * @brief Append dedicated compile options for a specific source file.
 * @param[in] target      the target.
 * @param[in] target_file source file path (ignored on no match).
 * @param[in] opt         the option.
 */
void _add_options_for(target_t *target, const char *target_file,
                       const char *opt);

/**
 * @brief Recursively collect all regular files under dir, sorted with
 *        strcmp (full relative paths).
 *
 * The bottom layer of the each_file() macro: recurses into
 * subdirectories, collects all regular files (not directories), sorts
 * ascending with strcmp for deterministic traversal, and returns a
 * NULL-terminated array.
 *
 * @param[in] dir the directory path.
 * @return malloc-allocated NULL-terminated path array; the caller
 *         frees each element and then the array itself; NULL when the
 *         directory does not exist / is unreadable / OOM (then the
 *         each_file loop body runs zero times).
 */
char **_list_files(const char *dir);

/**
 * @brief Parse a test file's header and configure the target, returning
 *        the location of the test executable.
 *
 * For default_test(). The full parsing rules and scope semantics are
 * documented at the implementation in build.c; key points:
 * - only the consecutive `//` lines at the start of the file are
 *   processed (CRLF-compatible); processing stops at the first
 *   non-`//` line;
 * - when the first token matches a keyword (Source / Include /
 *   Library / Link / Option, case-sensitive), the scope switches, and
 *   the rest of the line after the keyword (trimmed) is one complete
 *   entry of that scope; a non-keyword line belongs wholly to the
 *   current scope (initially = Source);
 * - entries are applied as they are read (_add_sources /
 *   _add_include_path / _add_lib_path / _add_link_lib / option_tail
 *   appends); the test file itself is always the first source file;
 * - silently skipped when the extension is not in {.c,.cpp,.cc,.cxx};
 *   on a read failure or entry failure (OOM), returns NULL and clears
 *   the applied entries (free_target_contents);
 * - on success, target->name becomes the "tests/" prefix and the last
 *   extension removed (tests/test.c -> test, tests/a/test.c ->
 *   a/test), and returns os_path_join("test", name) (.exe appended on
 *   Windows) - kept in sync with the set_output_final_path("test")
 *   literal inside default_test().
 *
 * @param[in] target the target (its configuration will be filled in).
 * @param[in] file   the test file path.
 * @return malloc-allocated location of the test executable; the
 *         caller must free it; NULL when skipped or on failure.
 */
char *parse_test_c(target_t *target, const char *file);

/**
 * @brief Run the target's full build flow (dispatched on a switch over
 *        target->target_type).
 *
 * Handled per artifact type:
 * - FORGE_EXECUTABLE / FORGE_STATIC_LIB / FORGE_SHARED_LIB: two
 *   passes - the first compiles source files into intermediate
 *   artifacts (FORGE_SOURCE -> FORGE_OBJ); the second aggregates the
 *   intermediate artifacts into the final artifact (FORGE_OBJ -> that
 *   type);
 * - FORGE_OBJ / FORGE_ASM: a single pass - directly produces
 *   .o/.obj or .s/.asm;
 * - FORGE_SOURCE: not compiled; each source file is copied verbatim
 *   under <output>/<target-name>/;
 * - FORGE_CUSTOM (and unknown values): not executed, returns -1
 *   directly.
 *
 * Default artifact layout (output_path is the root; the obj/output
 * subdirectory names reserve room for configuration):
 * - intermediate artifacts of the two-pass types live under
 *   <root>/opt/; single-pass artifacts and all final exports
 *   (executable / library / .o / .s / copied source files) live under
 *   <root>/output/;
 * - in-project source files mirror their relative directories (e.g.
 *   src/build/main.c -> opt/src/build/main.c); out-of-project source
 *   files are flattened from their absolute paths (separators to '_',
 *   literal '_' to "%5F", literal '%' to "%25", e.g. /usr/x/y.c ->
 *   opt/_usr_x_y.o); FORGE_SOURCE copies follow the same rule under
 *   output/<target-name>/.
 *
 * Symbol export arguments are added when linking shared libraries and
 * executables, so the function_* symbols stay visible to the runtime
 * (dlsym / GetProcAddress). _compile does not free the target: at the
 * target(x) block's update step, free_target_contents releases it
 * (the single release point).
 *
 * Incremental compilation (applies only to the compile pass: the
 * first pass of two-pass types and the single pass of FORGE_OBJ /
 * FORGE_ASM; the link pass always runs, FORGE_SOURCE copying does not
 * participate):
 * - each intermediate artifact `<obj>` has two companion files:
 *   `<obj>.d` (the header dependency list, generated by the compiler)
 *   and `<obj>.meta` (the file's compile command, written back after
 *   a successful compile);
 * - a source file is recompiled when any of the following holds: obj
 *   missing | .d missing | .meta missing | .meta does not match the
 *   current compile command | source mtime later than obj | any
 *   header in .d with an mtime later than obj (timestamps are
 *   millisecond-precision, strict `>` comparison);
 * - an all-whitespace .d means "no header dependencies"; a parse
 *   failure (corruption) is treated as needing a recompile; .meta is
 *   written back only after the whole pass compiles successfully -
 *   a mid-way failure writes nothing, and the next pass safely
 *   recompiles;
 * - MSVC dependency generation is described in forge_compiler.h:
 *   versions >= 19.27 use /sourceDependencies; otherwise an empty .d
 *   is written after a successful compile (header updates are not
 *   matched).
 *
 * -j / -v arguments (parsed via forge_parse_args(); see its docs):
 * - `-j <N>`: the compile pass (the first pass of two-pass types and
 *   the single pass of FORGE_OBJ/FORGE_ASM) runs N-way in parallel,
 *   one subprocess per file; after it finishes, the whole block is
 *   printed as `path (x.x ms)` (the path is relative to the project
 *   root, the directory containing the make executable; paths outside
 *   the root are shown absolute); in the link pass with gcc/clang,
 *   LTO enabled and N > 1, -flto is rewritten to -flto=N; with MSVC
 *   and N > 1, /CGTHREADS:N is appended (cap 8); N == 1 keeps bare
 *   -flto (the link pass always runs; it never participates in meta);
 * - `-v`: each file's output block appends the full compile command
 *   and the compiler's captured stdout/stderr after the
 *   `path (elapsed)` line; without -v, only path and elapsed are
 *   printed;
 * - under multithreading, each block is printed at once inside a
 *   mutex; blocks are never interleaved; when any file exits
 *   non-zero, no new tasks are dispatched, in-flight tasks are
 *   awaited, the first failing exit code is returned in task order,
 *   and no .meta is written back (consistent with incremental
 *   "write only on whole-pass success"); the failed file's captured
 *   output block is attached after its path line whether or not -v;
 * - FORGE_SOURCE copying and the link pass stay serial.
 *
 * @param[in] target the target configuration.
 * @param[in] argc   the argument count passed by the task function.
 * @param[in] argv   the argument array passed by the task function.
 * @return 0 when all steps succeed; -1 when any command fails,
 *         an argument is invalid, or the target type is FORGE_CUSTOM
 *         (unsupported).
 */
int _compile(target_t *target, int argc, char **argv);

#endif
