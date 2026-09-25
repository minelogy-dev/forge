/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file build.c
 * @brief Build DSL runtime implementation (the __-prefixed
 * interfaces declared in build.h).
 *
 * Responsibilities:
 * - Config collection: 14 typed functions (_set_* / _add_*;
 *   body contract: every target-config change must go through
 *   _set / _add; direct field writes are forbidden; the only
 *   exceptions are _add_options_for's per-file cflags and
 *   parse_test_c's naming bookkeeping, both noted in place)
 *   maintain the target via the sole _set / _add write points;
 * - Compile & link: _compile() flattens the source-file chain
 *   and advances it per the state machine SOURCE->OBJ->{EXE/
 *   SHARED_LIB (via linker/compiler)/STATIC_LIB (via archiver)};
 *   a compile round is producer/consumer parallel: the main
 *   thread enqueues jobs on demand (needs_rebuild incremental
 *   filter) into a shared queue; os_jobs consumer threads block
 *   on os_cond for tasks and run them; join is the phase
 *   barrier. Two-step intermediates live under
 *   <output>/opt/<target name>/; one-step types (FORGE_OBJ/
 *   FORGE_ASM) go straight to output/; FORGE_SOURCE is plain
 *   copy;
 * - Command contract: command-generating functions return a
 *   strv_t struct array terminated by an "empty strv_t"
 *   (strs=NULL, idx=0, length=0); each element is one argv
 *   (element 0 = program name); entries whose argv[0]=="echo"
 *   with a redirection operator are write-file directives (the
 *   display layer keeps the `echo "..." > file` syntax): only
 *   the _compile execution path special-cases them, writing via
 *   fopen "w"/"a" directly, never through a shell, and splitting
 *   multi-line content into several `>>` entries for readability;
 * - Symbol export: platform export flags are appended when
 *   linking shared libraries and executables (Linux -rdynamic,
 *   macOS -Wl,-export_dynamic, Windows GCC/CLANG
 *   -Wl,--export-all-symbols; MSVC adds /EXPORT: per the
 *   exports list), ensuring function_* symbols are visible to
 *   the runtime (dlsym / GetProcAddress);
 * - Memory reclamation: the target(x) loop step releases
 *   uniformly via free_target_contents (the sole free point);
 *   neither _compile nor parse_test_c frees the target.
 */
#include "build.h"
#include "export.h"
#include "forge_archiver.h"
#include "forge_assembler.h"
#include "forge_compiler.h"
#include "forge_linker.h"
#include "forge_opt.h"
#include "forge_os.h"
#include "forge_strv.h"
#include "forge_symbol_lister.h"
#include "forge_def.h"
#include "forge_type.h"
#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef target
#undef function

static void clean_sources(source_t *sources) {
  while (sources != NULL) {
    source_t *next = sources->next;
    source_node_free(sources);
    sources = next;
  }
}

/* Free a flattened source_t array (_compile's flat[]: nodes are
   embedded in the array, so per-node source_node_free doesn't apply) */
static void clean_source_array(source_t *flat, size_t n) {
  if (!flat)
    return;
  for (size_t i = 0; i < n; i++) {
    free(flat[i].file);
    strv_destroy(&flat[i].cflags);
  }
  free(flat);
}

/* Append the single-level source files under dir (path, already
   made absolute by the caller via os_listdir) to the chain tail and
   return the new tail; NULL if the dir is missing or unreadable.
   (Fix: the old implementation returned without ever assigning ret,
   so the _add_sources dir branch always failed) */
static source_t *add_source_path(source_t *tail, const target_t target,
                                 char *path) {
  int n = 0;
  char **names = os_listdir(path, &n);
  if (!names)
    return NULL;
  for (int i = 0; i < n; i++) {
    char *full = os_path_join(path, names[i]);
    if (!full)
      continue;
    if (os_path_is_file(full) && is_source_ext(names[i], target.language)) {
      source_t *s = source_new(full);
      if (s) {
        tail->next = s;
        tail = s;
      }
    }
    free(full);
  }
  for (int i = 0; i < n; i++)
    free(names[i]);
  free(names);
  return tail;
}

/* Recursive variant: a failed subdirectory is best-effort (keep
   going with the other entries) and does not fail the whole */
static source_t *add_source_pathr(source_t *tail, const target_t target,
                                  char *path) {
  int n = 0;
  char **names = os_listdir(path, &n);
  if (!names)
    return NULL;
  for (int i = 0; i < n; i++) {
    char *full = os_path_join(path, names[i]);
    if (!full)
      continue;
    if (os_path_is_dir(full)) {
      source_t *s = add_source_pathr(tail, target, full);
      if (s)
        tail = s;
    }
    if (os_path_is_file(full) && is_source_ext(names[i], target.language)) {
      source_t *s = source_new(full);
      if (s) {
        tail->next = s;
        tail = s;
      }
    }
    free(full);
  }
  for (int i = 0; i < n; i++)
    free(names[i]);
  free(names);
  return tail;
}

/* Attach a source chain (head node chain_head, tail node
   chain_tail) to the target's chain tail; on an empty target it
   becomes the chain head (fix: the old implementation segfaulted
   dereferencing sources_tail on an empty chain) */
static void chain_append(target_t *target, source_t *chain_head,
                         source_t *chain_tail) {
  if (target->sources_tail)
    target->sources_tail->next = chain_head;
  else
    target->sources = chain_head;
  target->sources_tail = chain_tail;
}

/* Directory hook: append a directory-scan block after the chain
   tail and return the new tail */
static source_t *append_source_dir(source_t *tail, const target_t target,
                                   char *path, int recursive) {
  char *abs = os_path_abs(path);
  if (!abs)
    return NULL;
  source_t *s = recursive ? add_source_pathr(tail, target, abs)
                          : add_source_path(tail, target, abs);
  free(abs);
  return s;
}

static int strv_form_va_append(strv_t *strv, int count, va_list valist) {
  strv_t new;
  if (strv_init(&new)) {
    return 1;
  }
  while (count--) {
    if (strv_append(&new, va_arg(valist, char *))) {
      return 1;
    }
  }
  for (int i = 0; i < new.count; i++) {
    strv_append_non_copy(strv, new.strs[i]);
  }
  free(new.strs);
  return 0;
}
static int strv_form_va(strv_t *strv, int count, va_list valist) {
  strv_t new;
  if (strv_init(&new)) {
    return 1;
  }
  while (count--) {
    if (strv_append(&new, va_arg(valist, char *))) {
      strv_destroy(&new);
      return 1;
    }
  }
  strv_destroy(strv);
  *strv = new;
  return 0;
}

/* ------------------------ Typed config wrappers ------------------------
 *
 * Layered contract: macros = thin shells -> this group of typed
 * functions. Function bodies may be arbitrarily complex (loops,
 * conditions, helper calls all allowed); the one hard constraint
 * is that every modification to the target configuration must go
 * through _set / _add. Two exceptions, both noted:
 * - _add_options_for: per-file cflags have no corresponding enum,
 *   so the source is written directly;
 * - parse_test_c's rewrite of target->name (naming bookkeeping).
 */

int _set_toolchain(target_t *target, compiler_type_t c) {
  return _set(target, LOCATION, TARGET_COMPILER, 1, c);
}

int _set_language(target_t *target, target_language_t l) {
  return _set(target, LOCATION, TARGET_LANGUAGE, 1, l);
}

int _set_type(target_t *target, file_type_t v) {
  return _set(target, LOCATION, TARGET_TYPE, 1, v);
}

int _set_output_path(target_t *target, const char *p) {
  return _set(target, LOCATION, TARGET_OUTPUT_PATH, 1, p);
}

int _set_output_final_path(target_t *target, const char *p) {
  return _set(target, LOCATION, TARGET_OUTPUT_FINAL_PATH, 1, p);
}

int _set_output_intermediate_path(target_t *target, const char *p) {
  return _set(target, LOCATION, TARGET_OUTPUT_INTERMEDIATE_PATH, 1, p);
}

int _set_optimization(target_t *target, opt_level_t o) {
  return _set(target, LOCATION, COMPILER_OPTIMIZATION, 1, o);
}

int _set_standard(target_t *target, const char *s) {
  return _set(target, LOCATION, COMPILER_STANDARD, 1, s);
}

int _set_visibility(target_t *target, int v) {
  return _set(target, LOCATION, TARGET_VISIBILITY, 1, v);
}

/* add_sources is non-recursive by default: a directory -> a
   single-level scan (TARGET_SOURCE_PATHS); a file -> a single
   source node (TARGET_SOURCES) */
int _add_sources(target_t *target, const char *p) {
  if (!target || !p || !*p)
    return -1;
  if (os_path_is_dir(p))
    return _add(target, LOCATION, TARGET_SOURCE_PATHS, 1, p);
  return _add(target, LOCATION, TARGET_SOURCES, 1, p);
}

/* add_sources_r: directory -> recursive scan
   (TARGET_SOURCE_PATHS_R); file -> single source */
int _add_sources_r(target_t *target, const char *p) {
  if (!target || !p || !*p)
    return -1;
  if (os_path_is_dir(p))
    return _add(target, LOCATION, TARGET_SOURCE_PATHS_R, 1, p);
  return _add(target, LOCATION, TARGET_SOURCES, 1, p);
}

int _add_include_path(target_t *target, const char *p) {
  return _add(target, LOCATION, TARGET_INCLUDE_PATHS, 1, p);
}

int _add_lib(target_t *target, const char *n) {
  return _add(target, LOCATION, TARGET_LINK_LIBS, 1, n);
}

int _add_define(target_t *target, const char *name, const char *value) {
  /* Assemble name=value (the caller-supplied value carries its own
     quotes) and append it to the defines list; the strv copy owns
     the string, so the temp buffer can be freed safely. */
  size_t nl = strlen(name);
  size_t vl = strlen(value);
  char *buf = malloc(nl + 1 + vl + 1); /* name=value\0 */
  if (!buf)
    return -1;
  memcpy(buf, name, nl);
  buf[nl] = '=';
  memcpy(buf + nl + 1, value, vl + 1);
  int r = _add(target, LOCATION, COMPILER_ADD_DEFINES, 1, buf);
  free(buf);
  return r;
}

int _add_option(target_t *target, const char *o) {
  return _add(target, LOCATION, COMPILER_OPTIONS, 1, o);
}

int _add_lib_path(target_t *target, const char *p) {
  return _add(target, LOCATION, TARGET_LIB_PATHS, 1, p);
}

int _add_link_lib(target_t *target, const char *n) {
  return _add(target, LOCATION, TARGET_LINK_LIBS, 1, n);
}

/* Append per-file compile options for the given source file: match
   by content (strcmp, per the conventions doc) and append to that
   file's cflags. Noted exception: per-file cflags have no toolchain
   enum counterpart, so the source is written directly (not via
   _set / _add). */
void _add_options_for(target_t *target, const char *target_file,
                       const char *opt) {
  if (!target || !target_file || !opt)
    return;
  for (source_t *s = target->sources; s; s = s->next) {
    if (strcmp(s->file, target_file) == 0) {
      strv_append(&s->cflags, opt);
      return;
    }
  }
}

/* Memory-safety audit (completed 2026-09-13): va_arg consumption
   matches count (the macro side guarantees count >= 1); the *_args
   substructures rely on new_target initialization (tests and the
   DSL both go through the target() macro); duplicate setting of
   string fields and the TARGET_LIBRARIES container now
   release-then-overwrites; type tags are constrained by the macro
   literals (char* for strings, int for enums). */
int _set(target_t *target, const char *location, forge_option_t opt, int count,
          ...) {
  int ret = 0;
  source_t *sources;
  source_t *sources_tail;
  va_list valist;
  va_start(valist, count);
  switch (opt) {
  case TARGET_TYPE:
    target->target_type = va_arg(valist, file_type_t);
    break;
  case TARGET_LANGUAGE:
    target->language = va_arg(valist, target_language_t);
    break;
  case TARGET_ARCHIVER:
    target->toolchain.archiver = va_arg(valist, int);
    break;
  case TARGET_ARCHIVER_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.archiver_path);
      target->toolchain.archiver_path = _nv;
    }
    break;
  case TARGET_ASSEMBLER:
    target->toolchain.assembler = va_arg(valist, int);
    break;
  case TARGET_ASSEMBLER_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.assembler_path);
      target->toolchain.assembler_path = _nv;
    }
    break;
  case TARGET_COMPILER:
    target->toolchain.compiler = va_arg(valist, int);
    /* Family default: MSVC has no AR/ASM equivalent; the others
       are set to UNKNOWN and routed through the default branch (a
       clear implementability signal - don't pose as GCC-family
       tools) */
    if (target->toolchain.compiler == MSVC) {
      target->toolchain.archiver = UNKNOWN;
      target->toolchain.assembler = UNKNOWN;
      target->toolchain.linker = UNKNOWN;
      target->toolchain.symbol_lister = UNKNOWN;
    }
    break;
  case TARGET_COMPILER_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.compiler_path);
      target->toolchain.compiler_path = _nv;
    }
    break;
  case TARGET_LINKER:
    target->toolchain.linker = va_arg(valist, int);
    break;
  case TARGET_LINKER_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.linker_path);
      target->toolchain.linker_path = _nv;
    }
    break;
  case TARGET_SYMBOL_LISTER:
    target->toolchain.symbol_lister = va_arg(valist, int);
    break;
  case TARGET_SYMBOL_LISTER_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.symbol_lister_path);
      target->toolchain.symbol_lister_path = _nv;
    }
    break;
  case TARGET_VISIBILITY:
    target->toolchain.compiler_args->visibility = va_arg(valist, int);
    break;
  case TARGET_OUTPUT_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->output_path);
      target->output_path = _nv;
    }
    break;
  case TARGET_OUTPUT_FINAL_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->output_final_path);
      target->output_final_path = _nv;
    }
    break;
  case TARGET_OUTPUT_INTERMEDIATE_PATH:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->output_intermediate_path);
      target->output_intermediate_path = _nv;
    }
    break;
  case TARGET_LTO: { /* count==1: read once, applies to compile and link both */
    int lto = va_arg(valist, int);
    target->toolchain.compiler_args->lto_enabled = lto;
    target->toolchain.linker_args->lto_enabled = lto;
    break;
  }
  case TARGET_SOURCES:
    sources_tail = sources = source_new(va_arg(valist, char *));
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    count--;
    while (count--) {
      source_t *s = source_new(va_arg(valist, char *));
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail->next = s;
      sources_tail = s;
    }
    clean_sources(target->sources);
    target->sources = sources;
    target->sources_tail = sources_tail;
    break;
  case TARGET_SOURCE_PATHS:
    sources_tail = sources = source_new("t");
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    while (count--) {
      source_t *s = append_source_dir(sources_tail, *target,
                                      va_arg(valist, char *), 0);
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail = s;
    }
    clean_sources(target->sources);
    target->sources = sources->next;
    source_node_free(sources);
    target->sources_tail = sources_tail;
    break;
  case TARGET_SOURCE_PATHS_R:
    sources_tail = sources = source_new("t");
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    while (count--) {
      source_t *s = append_source_dir(sources_tail, *target,
                                      va_arg(valist, char *), 1);
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail = s;
    }
    clean_sources(target->sources);
    target->sources = sources->next;
    source_node_free(sources);
    target->sources_tail = sources_tail;
    break;
  case TARGET_INCLUDE_PATHS:
    if (strv_form_va(&target->include_paths, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LIB_PATHS:
    if (strv_form_va(&target->lib_paths, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LINK_LIBS:
    if (strv_form_va(&target->link_libs, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LIBRARIES: {
    size_t n = 0;
    library_t **na = malloc((size_t)count * sizeof(library_t *));
    if (!na) {
      ret = 1;
      goto cleanup;
    }
    while (count--) {
      na[n++] = va_arg(valist, library_t *);
    }
    free(target->libs); /* container is our own malloc; library_t* elements are borrowed */
    target->libs = na;
    target->libs_count = (int)n; /* fix: count has gone to -1 after the loop */
    break;
  }
  case TARGET_EXPORT_SYMBOL:
    if (strv_form_va(&target->toolchain.linker_args->export_symbol, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_OPTIONS:
    if (strv_form_va(&target->options, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case ARCHIVER_DETERMINISTIC:
    target->toolchain.archiver_args->deterministic = va_arg(valist, int);
    break;
  case ARCHIVER_VERBOSE:
    target->toolchain.archiver_args->verbose = va_arg(valist, int);
    break;
  case ARCHIVER_OPTIONS:
    if (strv_form_va(&target->toolchain.archiver_args->options, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case ASSEMBLER_DEBUG:
    target->toolchain.assembler_args->debug_enabled = va_arg(valist, int);
    break;
  case ASSEMBLER_SYNTAX_INTEL:
    target->toolchain.assembler_args->syntax_intel = va_arg(valist, int);
    break;
  case ASSEMBLER_NO_EXECSTACK:
    target->toolchain.assembler_args->no_execstack = va_arg(valist, int);
    break;
  case ASSEMBLER_FATAL_WARNINGS:
    target->toolchain.assembler_args->fatal_warnings = va_arg(valist, int);
    break;
  case ASSEMBLER_STATISTICS:
    target->toolchain.assembler_args->statistics = va_arg(valist, int);
    break;
  case ASSEMBLER_OPTIONS:
    if (strv_form_va(&target->toolchain.assembler_args->options, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case COMPILER_OPTIMIZATION:
    target->toolchain.compiler_args->optimization = va_arg(valist, opt_level_t);
    break;
  case COMPILER_STANDARD:
    {
      char *_nv = forge_strdup(va_arg(valist, char *));
      if (!_nv) { ret = 1; goto cleanup; }
      free(target->toolchain.compiler_args->standard);
      target->toolchain.compiler_args->standard = _nv;
    }
    break;
  case COMPILER_ADD_DEFINES:
    if (strv_form_va(&target->toolchain.compiler_args->defines, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case COMPILER_DEBUG:
    target->toolchain.compiler_args->debug_enabled = va_arg(valist, int);
    break;
  case COMPILER_PIC:
    target->toolchain.compiler_args->pic_enabled = va_arg(valist, int);
    break;
  case COMPILER_PIE:
    target->toolchain.compiler_args->pie_enabled = va_arg(valist, int);
    break;
  case COMPILER_LTO:
    target->toolchain.compiler_args->lto_enabled = va_arg(valist, int);
    break;
  case COMPILER_OPTIONS:
    if (strv_form_va(&target->toolchain.compiler_args->options, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LIB_PATHS:
    if (strv_form_va(&target->toolchain.linker_args->lib_search_paths, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LIBS:
    if (strv_form_va(&target->toolchain.linker_args->libraries, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LTO:
    target->toolchain.linker_args->lto_enabled = va_arg(valist, int);
    break;
  case LINKER_SHARED:
    target->toolchain.linker_args->shared_enabled = va_arg(valist, int);
    break;
  case LINKER_STATIC_CRT:
    target->toolchain.linker_args->static_crt = va_arg(valist, int);
    break;
  case LINKER_STRIP:
    target->toolchain.linker_args->strip_symbols = va_arg(valist, int);
    break;
  case LINKER_GC_SECTIONS:
    target->toolchain.linker_args->gc_sections = va_arg(valist, int);
    break;
  case LINKER_MAP_FILE: {
    char *_nv = forge_strdup(va_arg(valist, char *));
    if (!_nv) {
      ret = 1;
      goto cleanup;
    }
    free(target->toolchain.linker_args->map_file);
    target->toolchain.linker_args->map_file = _nv;
    break;
  }
  case LINKER_OPTIONS:
    if (strv_form_va(&target->toolchain.linker_args->options, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_EXPORT_SYMBOL:
    if (strv_form_va(&target->toolchain.linker_args->export_symbol, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case SYMBOL_LISTER_DEMANGLE:
    target->toolchain.symbol_lister_args->demangle = va_arg(valist, int);
    break;
  case SYMBOL_LISTER_NUMERIC_SORT:
    target->toolchain.symbol_lister_args->numeric_sort = va_arg(valist, int);
    break;
  case SYMBOL_LISTER_UNDEF_ONLY:
    target->toolchain.symbol_lister_args->undefined_only = va_arg(valist, int);
    break;
  case SYMBOL_LISTER_DYNAMIC:
    target->toolchain.symbol_lister_args->dynamic = va_arg(valist, int);
    break;
  case SYMBOL_LISTER_FORMAT: {
    char *_nv = forge_strdup(va_arg(valist, char *));
    if (!_nv) {
      ret = 1;
      goto cleanup;
    }
    free(target->toolchain.symbol_lister_args->format);
    target->toolchain.symbol_lister_args->format = _nv;
    break;
  }
  case SYMBOL_LISTER_OPTIONS:
    if (strv_form_va(&target->toolchain.symbol_lister_args->options, count,
                     valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  }
cleanup:
  va_end(valist);
  if (ret != 0) {
    fprintf(stderr, "Error at %s\n", location);
    exit(EXIT_FAILURE);
  }
  return ret;
}
#define FE(name)                                                               \
  do {                                                                         \
    fprintf(stderr, "You can't add(%s, ...)\n", name);                         \
    exit(EXIT_FAILURE);                                                        \
  } while (0)
int _add(target_t *target, const char *location, forge_option_t opt, int count,
          ...) {
  int ret = 0;
  source_t *sources;
  source_t *sources_tail;
  va_list valist;
  va_start(valist, count);
  switch (opt) {
  case TARGET_TYPE:
    FE("TARGET_TYPE");
    break;
  case TARGET_LANGUAGE:
    FE("TARGET_LANGUAGE");
    break;
  case TARGET_ARCHIVER:
    FE("TARGET_ARCHIVER");
    break;
  case TARGET_ARCHIVER_PATH:
    FE("TARGET_ARCHIVER_PATH");
    break;
  case TARGET_ASSEMBLER:
    FE("TARGET_ASSEMBLER");
    break;
  case TARGET_ASSEMBLER_PATH:
    FE("TARGET_ASSEMBLER_PATH");
    break;
  case TARGET_COMPILER:
    FE("TARGET_COMPILER");
    break;
  case TARGET_COMPILER_PATH:
    FE("TARGET_COMPILER_PATH");
    break;
  case TARGET_LINKER:
    FE("TARGET_LINKER");
    break;
  case TARGET_LINKER_PATH:
    FE("TARGET_LINKER_PATH");
    break;
  case TARGET_SYMBOL_LISTER:
    FE("TARGET_SYMBOL_LISTER");
    break;
  case TARGET_SYMBOL_LISTER_PATH:
    FE("TARGET_SYMBOL_LISTER_PATH");
    break;
  case TARGET_VISIBILITY:
    FE("TARGET_VISIBILITY");
    break;
  case TARGET_OUTPUT_PATH:
    FE("TARGET_OUTPUT_PATH");
    break;
  case TARGET_OUTPUT_FINAL_PATH:
    FE("TARGET_OUTPUT_FINAL_PATH");
    break;
  case TARGET_OUTPUT_INTERMEDIATE_PATH:
    FE("TARGET_OUTPUT_INTERMEDIATE_PATH");
    break;
  case TARGET_LTO:
    FE("TARGET_LTO");
    break;
  case TARGET_SOURCES:
    sources_tail = sources = source_new(va_arg(valist, char *));
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    count--;
    while (count--) {
      source_t *s = source_new(va_arg(valist, char *));
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail->next = s;
      sources_tail = s;
    }
    chain_append(target, sources, sources_tail);
    break;
  case TARGET_SOURCE_PATHS:
    sources_tail = sources = source_new("t");
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    while (count--) {
      source_t *s = append_source_dir(sources_tail, *target,
                                      va_arg(valist, char *), 0);
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail = s;
    }
    chain_append(target, sources->next, sources_tail);
    source_node_free(sources); /* free the dummy node (leak fix) */
    break;
  case TARGET_SOURCE_PATHS_R:
    sources_tail = sources = source_new("t");
    if (sources == NULL) {
      ret = 1;
      goto cleanup;
    }
    while (count--) {
      source_t *s = append_source_dir(sources_tail, *target,
                                      va_arg(valist, char *), 1);
      if (s == NULL) {
        ret = 1;
        clean_sources(sources);
        goto cleanup;
      }
      sources_tail = s;
    }
    chain_append(target, sources->next, sources_tail);
    source_node_free(sources); /* free the dummy node (leak fix) */
    break;
  case TARGET_INCLUDE_PATHS:
    if (strv_form_va_append(&target->include_paths, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LIB_PATHS:
    if (strv_form_va_append(&target->lib_paths, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LINK_LIBS:
    if (strv_form_va_append(&target->link_libs, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_LIBRARIES: {
    if (count <= 0)
      break; /* empty append: avoid the implementation-defined return of realloc(...,0) */
    size_t n = (size_t)target->libs_count;
    size_t extra = (size_t)count;
    library_t **na =
        realloc(target->libs, (n + extra) * sizeof(library_t *));
    if (!na) {
      ret = 1;
      goto cleanup;
    }
    target->libs = na;
    while (extra--) {
      na[n++] = va_arg(valist, library_t *); /* fix: added n++ */
    }
    target->libs_count = (int)n;
    break;
  }
  case TARGET_EXPORT_SYMBOL:
    if (strv_form_va_append(&target->toolchain.linker_args->export_symbol,
                            count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case TARGET_OPTIONS:
    if (strv_form_va_append(&target->options, count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case ARCHIVER_DETERMINISTIC:
    FE("ARCHIVER_DETERMINISTIC");
    break;
  case ARCHIVER_VERBOSE:
    FE("ARCHIVER_VERBOSE");
    break;
  case ARCHIVER_OPTIONS:
    if (strv_form_va_append(&target->toolchain.archiver_args->options, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case ASSEMBLER_DEBUG:
    FE("ASSEMBLER_DEBUG");
    break;
  case ASSEMBLER_SYNTAX_INTEL:
    FE("ASSEMBLER_SYNTAX_INTEL");
    break;
  case ASSEMBLER_NO_EXECSTACK:
    FE("ASSEMBLER_NO_EXECSTACK");
    break;
  case ASSEMBLER_FATAL_WARNINGS:
    FE("ASSEMBLER_FATAL_WARNINGS");
    break;
  case ASSEMBLER_STATISTICS:
    FE("ASSEMBLER_STATISTICS");
    break;
  case ASSEMBLER_OPTIONS:
    if (strv_form_va_append(&target->toolchain.assembler_args->options, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case COMPILER_OPTIMIZATION:
    FE("COMPILER_OPTIMIZATION");
    break;
  case COMPILER_STANDARD:
    FE("COMPILER_STANDARD");
    break;
  case COMPILER_ADD_DEFINES:
    if (strv_form_va_append(&target->toolchain.compiler_args->defines, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case COMPILER_DEBUG:
    FE("COMPILER_DEBUG");
    break;
  case COMPILER_PIC:
    FE("COMPILER_PIC");
    break;
  case COMPILER_PIE:
    FE("COMPILER_PIE");
    break;
  case COMPILER_LTO:
    FE("COMPILER_LTO");
    break;
  case COMPILER_OPTIONS:
    if (strv_form_va_append(&target->toolchain.compiler_args->options, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LIB_PATHS:
    if (strv_form_va_append(&target->toolchain.linker_args->lib_search_paths,
                            count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LIBS:
    if (strv_form_va_append(&target->toolchain.linker_args->libraries, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_LTO:
    FE("LINKER_LTO");
    break;
  case LINKER_SHARED:
    FE("LINKER_SHARED");
    break;
  case LINKER_STATIC_CRT:
    FE("LINKER_STATIC_CRT");
    break;
  case LINKER_STRIP:
    FE("LINKER_STRIP");
    break;
  case LINKER_GC_SECTIONS:
    FE("LINKER_GC_SECTIONS");
    break;
  case LINKER_MAP_FILE:
    FE("LINKER_MAP_FILE");
    break;
  case LINKER_OPTIONS:
    if (strv_form_va_append(&target->toolchain.linker_args->options, count,
                            valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case LINKER_EXPORT_SYMBOL:
    if (strv_form_va_append(&target->toolchain.linker_args->export_symbol,
                            count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  case SYMBOL_LISTER_DEMANGLE:
    FE("SYMBOL_LISTER_DEMANGLE");
    break;
  case SYMBOL_LISTER_NUMERIC_SORT:
    FE("SYMBOL_LISTER_NUMERIC_SORT");
    break;
  case SYMBOL_LISTER_UNDEF_ONLY:
    FE("SYMBOL_LISTER_UNDEF_ONLY");
    break;
  case SYMBOL_LISTER_DYNAMIC:
    FE("SYMBOL_LISTER_DYNAMIC");
    break;
  case SYMBOL_LISTER_FORMAT:
    FE("SYMBOL_LISTER_FORMAT");
    break;
  case SYMBOL_LISTER_OPTIONS:
    if (strv_form_va_append(&target->toolchain.symbol_lister_args->options,
                            count, valist)) {
      ret = 1;
      goto cleanup;
    }
    break;
  }
cleanup:
  va_end(valist);
  if (ret != 0) {
    fprintf(stderr, "Error at %s\n", location);
    exit(EXIT_FAILURE);
  }
  return ret;
}
#undef FE

/* ------------------------ Path layout (defaults) ------------------------ */

/* Subdirectory under output_path (returns the plain subdir name
   when output_path is NULL) */
static char *sub_dir(const target_t *t, const char *sub) {
  if (t->output_path && *t->output_path)
    return os_path_join(t->output_path, sub);
  return forge_strdup(sub);
}

/* Target path under <output_intermediate_path> (when set) or
   <output_path>/<fallback>/, with mirrored/flattened naming */
static char *obj_path_for(const target_t *t, const char *file, const char *ext,
                          const char *fallback) {
  char *rel = os_path_file_rel(file, ext);
  if (!rel)
    return NULL;
  char *base = (t->output_intermediate_path && *t->output_intermediate_path)
                   ? forge_strdup(t->output_intermediate_path)
                   : sub_dir(t, fallback);
  if (!base) {
    free(rel);
    return NULL;
  }
  char *out = os_path_join(base, rel);
  free(base);
  free(rel);
  return out;
}

/* Ensure the parent directory of path exists */
static void ensure_parent_dir(const char *path) {
  if (!path)
    return;
  char *dir = os_path_dirname(path);
  if (dir) {
    os_mkdir_r(dir);
    free(dir);
  }
}

/* ------------------------ Final artifact naming ------------------------ */

static char *base_with(const char *name, const char *prefix,
                       const char *suffix) {
  if (!name) {
    return NULL;
  }
  size_t pl = prefix ? strlen(prefix) : 0;
  size_t nl = strlen(name);
  size_t sl = suffix ? strlen(suffix) : 0;
  char *out = malloc(pl + nl + sl + 1);
  if (!out)
    return NULL;
  if (pl)
    memcpy(out, prefix, pl);
  memcpy(out + pl, name, nl);
  if (sl)
    memcpy(out + pl + nl, suffix, sl);
  out[pl + nl + sl] = '\0';
  return out;
}

/* Final artifact path: under <output_path>/output/, with
   prefix/suffix appended per target type */
static char *artifact_path(const target_t *t) {
  const char *name = t->name ? t->name : "output";
  char *base = NULL;
  switch (t->target_type) {
  case FORGE_STATIC_LIB:
#if defined(FORGE_OS_WINDOWS)
    base = base_with(name, NULL, ".lib");
#else
    base = base_with(name, "lib", ".a");
#endif
    break;
  case FORGE_SHARED_LIB:
#if defined(FORGE_OS_WINDOWS)
    base = base_with(name, NULL, ".dll");
#elif defined(__APPLE__) || defined(__MACH__)
    base = base_with(name, "lib", ".dylib");
#else
    base = base_with(name, "lib", ".so");
#endif
    break;
  case FORGE_EXECUTABLE:
#if defined(FORGE_OS_WINDOWS)
    base = base_with(name, NULL, os_exe_ext());
#else
    base = base_with(name, NULL, NULL);
#endif
    break;
  default:
    return NULL;
  }
  if (!base)
    return NULL;
  /* Final artifact dir: used as-is (borrowed) when
     output_final_path is set, otherwise <output_path>/output/ */
  char *dir = (t->output_final_path && *t->output_final_path)
                  ? forge_strdup(t->output_final_path)
                  : sub_dir(t, "output");
  if (!dir) {
    free(base);
    return NULL;
  }
  char *full = os_path_join(dir, base);
  free(dir);
  free(base);
  return full;
}


/* ------------------------ Incremental build check ------------------------ */

/* Append a suffix to the end of path (e.g. ".d" / ".meta" on an
   obj); NULL on failure */
static char *with_suffix(const char *path, const char *suffix) {
  if (!path || !suffix)
    return NULL;
  size_t pl = strlen(path);
  size_t sl = strlen(suffix);
  char *out = malloc(pl + sl + 1);
  if (!out)
    return NULL;
  memcpy(out, path, pl);
  memcpy(out + pl, suffix, sl + 1);
  return out;
}

/* Read a file into a malloc'd NUL-terminated buffer; NULL on failure */
static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[n] = '\0';
  return buf;
}

/* Write fixed-length content to a file (overwrite); 0 on success, -1 on failure */
static int write_text(const char *path, const char *buf, size_t len) {
  if (!path || !buf)
    return -1;
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  int ret = 0;
  if (len > 0 && fwrite(buf, 1, len, f) != len)
    ret = -1;
  if (fclose(f) != 0)
    ret = -1;
  return ret;
}

static int is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Minimal MSVC JSON: locate the "Includes" key and extract the
   quoted strings inside its [ ... ], handling "\\" and "\"" escapes;
   -1 if the structure is malformed */
static int parse_msvc_json(char *s, char ***deps, int *count) {
  char *key = strstr(s, "\"Includes\"");
  if (!key)
    return -1;
  char *lb = strchr(key, '[');
  if (!lb)
    return -1;
  char *rb = strchr(lb, ']');
  if (!rb)
    return -1;

  char *p = lb + 1;
  strv_t *arr = strv_new();
  int n = 0;
  while (p < rb) {
    while (p < rb &&
           (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
      p++;
    if (p >= rb)
      break;
    if (*p != '"') {
      strv_free(arr);
      return -1;
    }
    p++;

    strbuf sb;
    sb_init(&sb);
    int bad = 0;
    while (p < rb && *p != '"') {
      if (*p == '\\' && p + 1 < rb) {
        char e = p[1];
        if (e == '\\' || e == '"') {
          if (sb_char(&sb, e)) {
            bad = 1;
            break;
          }
          p += 2;
        } else {
          if (sb_char(&sb, '\\')) {
            bad = 1;
            break;
          }
          p++;
        }
      } else {
        if (sb_char(&sb, *p)) {
          bad = 1;
          break;
        }
        p++;
      }
    }
    if (bad) {
      sb_free(&sb);
      strv_free(arr);
      return -1;
    }
    if (p >= rb || *p != '"') { /* unterminated quote */
      sb_free(&sb);
      strv_free(arr);
      return -1;
    }
    p++; /* skip the closing quote */
    if (strv_append(arr, sb.buf ? sb.buf : "")) {
      sb_free(&sb);
      strv_free(arr);
      return -1;
    }
    n++;
    sb_free(&sb);
  }

  *deps = arr->strs;
  if (count)
    *count = n;
  free(arr);
  return 0;
}

/* make syntax: one dependency after the first ':' at a time;
   backslash-newline continuations, "\ " escaped spaces. Returns -1
   on missing ':' or malformed structure */
static int parse_make_deps(char *s, char ***deps, int *count) {
  char *colon = strchr(s, ':');
  if (!colon)
    return -1;

  char *p = colon + 1;
  strv_t *arr = strv_new();
  int n = 0;
  strbuf cur;
  sb_init(&cur);
  int bad = 0;

  while (*p) {
    if (*p == '\\' && p[1] == '\n') { /* line continuation */
      p += 2;
      while (*p && (*p == ' ' || *p == '\t' || *p == '\r'))
        p++;
    } else if (*p == '\\' && p[1] == '\r' && p[2] == '\n') { /* continuation (CRLF) */
      p += 3;
      while (*p && (*p == ' ' || *p == '\t'))
        p++;
    } else if (*p == '\\' && p[1] == ' ') { /* escaped space */
      if (sb_char(&cur, ' ')) {
        bad = 1;
        break;
      }
      p += 2;
    } else if (is_ws(*p)) { /* dependency separator */
      if (cur.len > 0) {
        if (strv_append(arr, cur.buf)) {
          bad = 1;
          break;
        }
        n++;
        sb_free(&cur);
        sb_init(&cur);
      }
      p++;
    } else {
      if (sb_char(&cur, *p)) {
        bad = 1;
        break;
      }
      p++;
    }
  }
  if (!bad && cur.len > 0 && strv_append(arr, cur.buf))
    bad = 1;
  else if (!bad && cur.len > 0)
    n++;
  sb_free(&cur);

  if (bad) {
    strv_free(arr);
    return -1;
  }
  *deps = arr->strs;
  free(arr);
  if (count)
    *count = n;
  return 0;
}

/* Read the .d dependency list: all whitespace counts as "no
   deps" (success, count=0); a first non-whitespace char of '{' is
   MSVC JSON, otherwise make syntax; -1 on parse failure
   (recompile needed) */
static int read_deps(const char *dpath, char ***deps, int *count) {
  if (deps)
    *deps = NULL;
  if (count)
    *count = 0;

  char *data = read_file(dpath);
  if (!data)
    return -1;

  char *p = data;
  while (*p && is_ws(*p))
    p++;
  int ret;
  if (!*p) {
    ret = 0; /* empty .d: no header dependencies */
  } else if (*p == '{') {
    ret = parse_msvc_json(p, deps, count);
  } else {
    ret = parse_make_deps(p, deps, count);
  }
  free(data);
  return ret;
}

/* ===================== Compile execution (producer/consumer) =====================
 *
 * Scheduling: the main thread is only the producer - enqueue the
 * indices of files to compile and it's done; path derivation,
 * incremental check, command generation, execution and .meta
 * write-back all happen inside worker threads (each worker builds
 * its own forge_context_t from the shared round state and routes
 * itself), so the first compile command starts as soon as
 * possible. Consumers block on os_cond; join is the round barrier.
 *
 * Command contract: command-generating functions return a strv_t
 * struct array ending in an "empty strv_t" (strs=NULL, idx=0,
 * length=0); each element is one argv. Entries whose
 * argv[0]=="echo" and contain a redirection operator (">"/">>")
 * are write-file directives: only the _compile execution path
 * special-cases them - fopen("w"/"a") writes the content directly
 * (content elements joined by spaces, trailing newline appended),
 * never through a shell; the display layer keeps the echo syntax
 * for readability (multi-line content split into several ">>"
 * entries).
 */

/* Forward declarations (used by sub_compile before their definitions) */
static strv_t *per_file_cmd(const target_t *t, const source_t *s,
                            const char *obj, file_type_t src_type,
                            file_type_t trg_type, const char *root);
static int needs_rebuild(const char *obj, const char *src, const char *cmd);

/* Display path: files under the project root (root) are shown
   relative to it; files outside it, or when absolutizing fails,
   show the absolute path as-is */
static char *display_for(const char *root, const char *file) {
  char *abs = os_path_abs(file);
  if (!abs)
    return forge_strdup(file);

  const char *rel = NULL;
  if (root && *root) {
    size_t rl = strlen(root);
    while (rl > 1 && (root[rl - 1] == '/' || root[rl - 1] == '\\'))
      rl--;
    if (rl == 1 && root[0] == '/')
      rel = abs + 1;
    else if (strncmp(abs, root, rl) == 0 &&
             (abs[rl] == '\0' || abs[rl] == '/' || abs[rl] == '\\'))
      rel = abs + rl;
  }
  if (!rel)
    return abs; /* outside the root: absolute path as-is */

  while (*rel == '/' || *rel == '\\')
    rel++;
  if (!*rel)
    return abs;
  char *out = forge_strdup(rel);
  free(abs);
  if (!out)
    return forge_strdup(file);
  return out;
}

/* echo write-file special case (only in the _compile execution
   path): argv looks like {"echo", <content...>, ">"|">>", <target>}.
   Content elements joined by spaces, fopen("w"/"a") writes
   directly (trailing newline appended), never through a shell.
   Returns: 1 = handled as echo; 0 = a non-echo entry (run as a
   regular command); 1 with *err=-1 on write failure. */
static int echo_write_if(char **argv, int *err) {
  *err = 0;
  if (!argv || !argv[0] || strcmp(argv[0], "echo") != 0)
    return 0;
  const char *mode = NULL;
  int di = -1;
  for (int i = 1; argv[i]; i++) {
    if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
      mode = argv[i];
      di = i;
      break;
    }
  }
  if (!mode || !argv[di + 1] || argv[di + 2])
    return 0; /* no redirector/target, or trailing content: run as a regular command */
  strbuf sb;
  sb_init(&sb);
  for (int i = 1; i < di; i++) {
    if (i > 1)
      sb_char(&sb, ' ');
    sb_cat(&sb, argv[i]);
  }
  FILE *f = fopen(argv[di + 1], strcmp(mode, ">>") == 0 ? "a" : "w");
  if (!f) {
    sb_free(&sb);
    *err = -1;
    return 1;
  }
  if (sb.buf && sb.len)
    fwrite(sb.buf, 1, sb.len, f);
  fputc('\n', f); /* echo semantics: trailing newline */
  fclose(f);
  sb_free(&sb);
  return 1;
}

/* Execution result (printed as one mutex-protected block) */
typedef struct {
  strbuf out; /* concatenated captured output */
  int status; /* 0 success; else exit code; -1 command-gen/launch failure */
  double ms;
} exec_result;

/* Free a command sequence: strv_destroy every element + the array */
static void free_seq(strv_t *seq) {
  if (!seq)
    return;
  for (strv_t *it = seq; it->strs; it++)
    strv_destroy(it);
  free(seq);
}

/* Run a command sequence element by element (echo entries are
   written via fopen directly, the rest run as argv), stopping at
   the first failure; captured output is appended to res->out (the
   caller sb_inits it) */
static void exec_seq(strv_t *seq, exec_result *res) {
  uint64_t t0 = os_now_ms();
  res->status = 0;
  for (strv_t *it = seq; it && it->strs && res->status == 0; it++) {
    int eerr = 0;
    if (echo_write_if(it->strs, &eerr)) {
      if (eerr)
        res->status = -1;
      continue;
    }
    int st = -1;
    char *cap = os_execute_capture_all_status(it->strs[0], it->strs, "", NULL,
                                            &st);
    if (cap) {
      sb_catn(&res->out, cap, strlen(cap));
      free(cap);
    }
    if (st != 0)
      res->status = st;
  }
  uint64_t t1 = os_now_ms();
  res->ms = t1 >= t0 ? (double)(t1 - t0) : 0.0;
}

/* Print as one block (must be called while holding the round
   lock). verbosity is three-state:
   -1 = -q quiet: no output while commands succeed, only the error
       block on failure;
    0 = default: every executed command is always printed (path
       line + command line); captured output only on failure;
    1 = -v: every command is printed along with its captured
       output. */
static void print_result(const char *display, int verbosity,
                         const exec_result *res, strv_t *seq) {
  if (verbosity < 0 && res->status == 0)
    return; /* -q and success: print nothing */
  printf("%s (%.1f ms)\n", display, res->ms);
  /* every executed command is printed (incl. echo write entries) */
  for (strv_t *it = seq; it && it->strs; it++) {
    char *l = os_argv2line(it->strs);
    if (l) {
      printf("%s\n", l);
      free(l);
    }
  }
  /* captured output: always under -v; otherwise only on failure */
  if (verbosity > 0 || res->status != 0) {
    if (res->out.buf && res->out.len)
      fwrite(res->out.buf, 1, res->out.len, stdout);
  }
}

/* Dynamic ring queue: stores job (file) indices */
typedef struct {
  int *buf, cap, head, tail, count;
} job_queue;

static void queue_init(job_queue *q, int cap) {
  q->buf = malloc((size_t)cap * sizeof(int));
  q->cap = cap;
  q->head = q->tail = q->count = 0;
}

static int queue_push(job_queue *q, int idx) {
  if (q->count >= q->cap)
    return -1;
  q->buf[q->tail] = idx;
  q->tail = (q->tail + 1) % q->cap;
  q->count++;
  return 0;
}

static int queue_pop(job_queue *q, int *idx) {
  if (q->count == 0)
    return -1;
  *idx = q->buf[q->head];
  q->head = (q->head + 1) % q->cap;
  q->count--;
  return 0;
}

/* Worker count: the smaller of os_jobs (at least 1) and the
   number of tasks */
static int worker_count(int jobs, int tasks) {
  int j = jobs > 1 ? jobs : 1;
  return j < tasks ? j : tasks;
}

/* Shared state of one compile round: flat/dirs/extensions etc.
   are read-only after init; the queue and stop flag are
   read/written under the lock; per-file exit codes live in index
   slots (race-free) */
typedef struct {
  const target_t *t;
  source_t *flat;
  int n;
  const char *ext;   /* target extension (.o/.obj/.s/.asm) */
  char *mid_dir;     /* intermediate output dir (this round) */
  file_type_t src_type, trg_type;
  int msvc_no_deps;  /* MSVC without dep-gen support: write an empty .d after success */
  char *root;        /* display-path root (project root) */
  job_queue q;
  os_mutex lock;     /* protects q/stop/produced/print block */
  os_cond ready;
  int stop;          /* stop dispatching on failure (under lock) */
  int produced;      /* producer done (under lock) */
  int verbose;
  int *statuses;     /* per-file exit codes (consumers write slots) */
  unsigned char *ran; /* whether each file has run */
} round_t;

/* No path/command derivation "in the round" (under the lock); all
   of it lives in sub_compile: once a consumer grabs a file index -
   derive the obj/.d/.meta and display paths -> build the per-file
   ctx -> generate the command sequence -> incremental check -> if a
   rebuild is needed, run it and write back its own meta (written
   back on job success; on a round failure the written meta still
   matches the command, so the next round's check stays accurate) */
static void sub_compile(round_t *r, int idx) {
  const target_t *t = r->t;
  source_t *s = &r->flat[idx];

  char *obj = NULL, *dpath = NULL, *mpath = NULL, *display = NULL, *cmdline = NULL;
  strv_t *seq = NULL;
  exec_result res;
  memset(&res, 0, sizeof(res));
  sb_init(&res.out);

  /* 1. Path derivation (all inside the worker thread) */
  char *rel = os_path_file_rel(s->file, r->ext);
  if (rel) {
    obj = os_path_join(r->mid_dir, rel);
    free(rel);
  }
  if (!obj)
    goto oom;
  dpath = with_suffix(obj, ".d");
  mpath = with_suffix(obj, ".meta");
  display = display_for(r->root, s->file);
  if (!dpath || !mpath || !display)
    goto oom;

  /* 2. Command generation + incremental check */
  seq = per_file_cmd(t, s, obj, r->src_type, r->trg_type, r->root);
  if (!seq)
    goto genfail;
  cmdline = os_argv2line(seq[0].strs);
  if (!cmdline)
    goto oom;
  if (!needs_rebuild(obj, s->file, cmdline)) {
    free_seq(seq);
    free(cmdline);
    free(obj);
    free(dpath);
    free(mpath);
    free(display);
    sb_free(&res.out);
    return; /* incremental hit: skip, no execution slot taken */
  }

  /* 3. Execute + print + meta (print & stop-on-failure under lock) */
  ensure_parent_dir(obj);
  exec_seq(seq, &res);
  os_mutex_lock(&r->lock);
  r->ran[idx] = 1;
  r->statuses[idx] = res.status;
  print_result(display, r->verbose, &res, seq);
  if (res.status != 0)
    r->stop = 1;
  os_mutex_unlock(&r->lock);

  if (res.status == 0) {
    /* job succeeded: write back own .meta; MSVC w/o dep support: empty .d */
    size_t clen = strlen(cmdline);
    char *meta = malloc(clen + 2);
    if (meta) {
      memcpy(meta, cmdline, clen);
      meta[clen] = '\n';
      meta[clen + 1] = '\0';
      write_text(mpath, meta, clen + 1);
      free(meta);
    }
    if (r->msvc_no_deps)
      write_text(dpath, "", 0);
  }

  free_seq(seq);
  free(cmdline);
  free(obj);
  free(dpath);
  free(mpath);
  free(display);
  sb_free(&res.out);
  return;

oom:
  if (seq)
    free_seq(seq);
  free(cmdline);
  free(obj);
  free(dpath);
  free(mpath);
  free(display);
  sb_free(&res.out);
  os_mutex_lock(&r->lock);
  r->ran[idx] = 1;
  r->statuses[idx] = -1;
  r->stop = 1;
  os_mutex_unlock(&r->lock);
  return;

genfail:
  free(obj);
  free(dpath);
  free(mpath);
  free(display);
  sb_free(&res.out);
  os_mutex_lock(&r->lock);
  r->ran[idx] = 1;
  r->statuses[idx] = -1;
  r->stop = 1;
  os_mutex_unlock(&r->lock);
}

/* Consumer: blocks when the queue is empty and the producer has
   not finished (os_cond_wait; spurious wakeups are absorbed by the
   while condition); once a file index is taken, path derivation /
   incremental check / command generation / execution / meta all
   finish in the worker thread (see sub_compile); after a failure
   stops dispatch, no new jobs are taken */
static void consume_one(round_t *r) {
  int idx;
  os_mutex_lock(&r->lock);
  if (r->stop || queue_pop(&r->q, &idx) != 0) {
    os_mutex_unlock(&r->lock);
    return;
  }
  os_mutex_unlock(&r->lock);
  sub_compile(r, idx);
}

static void consumer(void *arg) {
  round_t *r = (round_t *)arg;
  for (;;) {
    os_mutex_lock(&r->lock);
    while (!r->stop && r->q.count == 0 && !r->produced)
      os_cond_wait(&r->ready, &r->lock);
    os_mutex_unlock(&r->lock);
    consume_one(r);
    os_mutex_lock(&r->lock);
    if (r->stop || (r->produced && r->q.count == 0)) {
      os_mutex_unlock(&r->lock);
      return;
    }
    os_mutex_unlock(&r->lock);
  }
}

/* Build the command sequence from a per-file ctx (compile round):
   returns a strv_t* array (ends with an empty one); ctx.files
   points at the first element of the struct array (&flat[i]) -
   files_count is always 1 in a compile round */
static strv_t *per_file_cmd(const target_t *t, const source_t *s,
                            const char *obj, file_type_t src_type,
                            file_type_t trg_type, const char *root) {
  forge_context_t ctx = {0};
  ctx.target = (target_t *)t;
  ctx.files = (source_t *)s;
  ctx.files_count = 1;
  ctx.output_file = (char *)obj;
  ctx.source_type = src_type;
  ctx.target_type = trg_type;
  ctx.root = (char *)root; /* project root: base for shortening command paths (borrowed) */
  return compiler(ctx);
}

/* Decide whether a source file needs a recompile. In order: obj
   missing | .d missing | .meta missing | .meta != cmd (trailing
   newline stripped) | source mtime > obj | any dependency mtime >
   obj; returns 1 if a rebuild is needed, else 0 */
static int needs_rebuild(const char *obj, const char *src, const char *cmd) {
  char *dpath = with_suffix(obj, ".d");
  char *mpath = with_suffix(obj, ".meta");
  if (!dpath || !mpath) {
    free(dpath);
    free(mpath);
    return 1;
  }

  int rebuild = 0;

  if (!os_path_is_file(obj)) {
    rebuild = 1;
    goto out;
  }
  if (!os_path_is_file(dpath)) {
    rebuild = 1;
    goto out;
  }
  char *meta = read_file(mpath);
  if (!meta) {
    rebuild = 1;
    goto out;
  }
  {
    size_t mlen = strlen(meta);
    while (mlen > 0 && (meta[mlen - 1] == '\n' || meta[mlen - 1] == '\r'))
      meta[--mlen] = '\0';
    if (strcmp(meta, cmd) != 0) {
      free(meta);
      rebuild = 1;
      goto out;
    }
  }
  free(meta);

  {
    uint64_t om = os_mtime_ms(obj);
    /* Missing source: os_mtime_ms would return 0 and silently
       decide not to rebuild, so absence must be checked explicitly
       (make's analog: "No rule to make target") */
    if (!os_path_is_file(src)) {
      rebuild = 1;
      goto out;
    }
    if (os_mtime_ms(src) > om) {
      rebuild = 1;
      goto out;
    }
    char **deps = NULL;
    int dc = 0;
    if (read_deps(dpath, &deps, &dc) != 0) {
      rebuild = 1; /* corrupted .d -> conservative rebuild */
      goto out;
    }
    for (int i = 0; i < dc; i++) {
      if (os_mtime_ms(deps[i]) > om) {
        rebuild = 1;
        break;
      }
    }
    for (char **p = deps; *p; p++)
      free(*p);
    free(deps);
  }

out:
  free(dpath);
  free(mpath);
  return rebuild;
}
/* Compile round: the producer (main thread) only "enqueues
   indices"; path derivation / incremental check / command
   generation / execution / meta all finish inside consumers
   (sub_compile, in worker threads), so the first compile command
   starts as soon as possible; join is the round barrier. Returns
   the first failing exit code (in job order, counting only
   executed jobs); jobs not taken after dispatch stops never run */
static int compile_round(const target_t *t, source_t *flat, int n,
                         char *mid_dir, const char *ext,
                         file_type_t src_type, file_type_t trg_type) {
  round_t r;
  memset(&r, 0, sizeof(r));
  r.t = t;
  r.flat = flat;
  r.n = n;
  r.ext = ext;
  r.mid_dir = mid_dir;
  r.src_type = src_type;
  r.trg_type = trg_type;
  r.msvc_no_deps =
      t->toolchain.compiler == MSVC && !forge_msvc_source_deps_supported(t);
  r.verbose = os_verbose; /* three-state pass-through: 0 default / 1 -v / -1 -q */
  r.root = os_path_abs(NULL); /* project root = launch cwd (shared by display and commands) */
  r.statuses = calloc((size_t)n, sizeof(int));
  r.ran = calloc((size_t)n, sizeof(unsigned char));
  queue_init(&r.q, n + 1);
  if (!r.statuses || !r.ran || !r.q.buf) {
    free(r.statuses);
    free(r.ran);
    free(r.q.buf);
    free(r.root);
    return -1;
  }
  os_mutex_init(&r.lock);
  os_cond_init(&r.ready);

  int workers = worker_count(os_jobs, n);
  os_thread *thr = NULL;
  int started = 0;
  if (workers > 1) {
    thr = malloc((size_t)workers * sizeof(os_thread));
    if (thr) {
      for (int k = 0; k < workers; k++) {
        if (os_thread_start(&thr[k], consumer, &r) != 0)
          break;
        started++;
      }
    }
  }

  /* Producer (main thread): enqueue all file indices and it's
     done, no derivation of any kind here; nothing more is enqueued
     once a failure stops dispatch */
  for (int i = 0; i < n; i++) {
    os_mutex_lock(&r.lock);
    if (r.stop) { /* stop check moved under the lock (lock-free read is a data race) */
      os_mutex_unlock(&r.lock);
      break;
    }
    queue_push(&r.q, i);
    os_cond_signal(&r.ready);
    os_mutex_unlock(&r.lock);
  }
  os_mutex_lock(&r.lock);
  r.produced = 1;
  os_cond_broadcast(&r.ready);
  os_mutex_unlock(&r.lock);

  for (int k = 0; k < started; k++)
    os_thread_join(&thr[k]);
  free(thr);

  /* thread-creation gap (or a single worker): the main thread consumes from the queue itself */
  int idx;
  for (;;) {
    os_mutex_lock(&r.lock);
    if (r.stop || queue_pop(&r.q, &idx) != 0) {
      os_mutex_unlock(&r.lock);
      break;
    }
    os_mutex_unlock(&r.lock);
    sub_compile(&r, idx);
  }

  /* result collection: first failure in job order (only executed jobs) */
  int ret = 0;
  for (int i = 0; i < n; i++) {
    if (r.ran[i] && r.statuses[i] != 0) {
      ret = r.statuses[i];
      break;
    }
  }

  free(r.statuses);
  free(r.ran);
  free(r.q.buf);
  free(r.root);
  return ret;
}

/* Final round (link/archive): generate the command sequence from
   the whole-target ctx and run it; on failure echo the output to
   stderr and return its exit code */
static int final_round(const target_t *t, source_t *flat, int n, char **objs,
                       const char *artifact, file_type_t src_type,
                       file_type_t trg_type, const char *root) {
  forge_context_t ctx = {0};
  ctx.target = (target_t *)t;
  ctx.files = flat;
  ctx.files_count = n;
  ctx.objs = objs;
  ctx.output_file = (char *)artifact;
  ctx.source_type = src_type;
  ctx.target_type = trg_type;
  ctx.root = (char *)root; /* project root: base for shortening link-command paths (borrowed) */
  strv_t *seq =
      trg_type == FORGE_STATIC_LIB ? archiver(ctx) : linker(ctx);
  if (!seq) {
    fprintf(stderr, "command generate failed (%s)\n",
            artifact ? artifact : "?");
    return -1;
  }
  exec_result res;
  memset(&res, 0, sizeof(res));
  sb_init(&res.out);
  exec_seq(seq, &res);
  /* Same output contract as the compile round: commands always
     print; captured output only on failure (or -v); display paths
     and commands are shortened against the project root alike */
  char *disp = root ? display_for(root, artifact ? artifact : "?") : NULL;
  print_result(disp ? disp : (artifact ? artifact : "?"), os_verbose,
               &res, seq);
  free(disp);
  if (res.status != 0)
    fprintf(stderr, "command failed (%d)\n", res.status);
  free_seq(seq);
  sb_free(&res.out);
  return res.status;
}

/* Intermediate root: <output_intermediate_path> || <output_path>/opt */
static char *obj_base(const target_t *t) {
  if (t->output_intermediate_path && *t->output_intermediate_path)
    return os_path_abs(t->output_intermediate_path);
  char *op =
      os_path_abs(t->output_path && *t->output_path ? t->output_path : NULL);
  char *base = op ? os_path_join(op, "opt") : NULL;
  free(op);
  return base;
}

/* One-round output directory: <output_intermediate_path> ||
   <output_final_path> || <output_path>/output */
static char *one_round_base(const target_t *t) {
  if (t->output_intermediate_path && *t->output_intermediate_path)
    return os_path_abs(t->output_intermediate_path);
  if (t->output_final_path && *t->output_final_path)
    return os_path_abs(t->output_final_path);
  char *op =
      os_path_abs(t->output_path && *t->output_path ? t->output_path : NULL);
  char *base = op ? os_path_join(op, "output") : NULL;
  free(op);
  return base;
}

int _compile(target_t *target, int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (!target)
    return -1;
  if (target->target_type == FORGE_CUSTOM)
    return -1;
  size_t n = 0;
  for (source_t *s = target->sources; s; s = s->next)
    n++;
  if (n == 0)
    return 0; /* no sources: don't compile, don't free (the target() step frees) */

  int ret = 0;
  int msvc = target->toolchain.compiler == MSVC;
  const char *obj_ext = msvc ? ".obj" : ".o";
  const char *asm_ext = msvc ? ".asm" : ".s";
  file_type_t tt = target->target_type;

  /* flatten chain -> source_t array (strdup file, strv_copy cflags) */
  source_t *flat = malloc(n * sizeof(source_t));
  if (!flat)
    return -1;
  {
    size_t i = 0;
    for (source_t *s = target->sources; s; s = s->next, i++) {
      flat[i].file = forge_strdup(s->file);
      flat[i].mtime = s->mtime;
      flat[i].next = i + 1 < n ? &flat[i + 1] : NULL;
      strv_init(&flat[i].cflags);
      strv_copy(&flat[i].cflags, &s->cflags);
      if (!flat[i].file) {
        clean_source_array(flat, n);
        return -1;
      }
    }
  }
  char **objs = calloc(n + 1, sizeof(char *));
  if (!objs) {
    clean_source_array(flat, n);
    return -1;
  }

  if (tt == FORGE_EXECUTABLE || tt == FORGE_SHARED_LIB ||
      tt == FORGE_STATIC_LIB) {
    /* round 1: SOURCE -> OBJ (area = <intermediate root>/<target name>/) */
    char *base = obj_base(target);
    char *mid = base
                    ? os_path_join(base,
                                   target->target_name
                                       ? target->target_name
                                       : (target->name ? target->name
                                                       : "output"))
                    : NULL;
    free(base);
    if (!mid) {
      ret = -1;
      goto cleanup;
    }
    for (size_t i = 0; i < n; i++) {
      char *rel = os_path_file_rel(flat[i].file, obj_ext);
      objs[i] = rel ? os_path_join(mid, rel) : NULL;
      free(rel);
      if (!objs[i]) {
        free(mid);
        ret = -1;
        goto cleanup;
      }
    }
    ret = compile_round(target, flat, (int)n, mid, obj_ext, FORGE_SOURCE,
                        FORGE_OBJ);
    free(mid);
    if (ret != 0)
      goto cleanup;
    /* Round 2: OBJ -> final artifact (linking via
       compiler-as-linker, static libs via the archiver; export
       flags and -fPIC are emitted by the backend per target_type) */
    char *artifact = artifact_path(target);
    if (!artifact) {
      ret = -1;
      goto cleanup;
    }
    ensure_parent_dir(artifact); /* ensure the artifact dir exists before round 2 */
    char *root = os_path_abs(NULL); /* project root = the cwd at startup */
    ret = final_round(target, flat, (int)n, objs, artifact, FORGE_OBJ, tt,
                      root);
    free(root);
    free(artifact);
    if (ret != 0)
      goto cleanup;
  } else if (tt == FORGE_OBJ || tt == FORGE_ASM) {
    /* single round: direct output, area = one_round_base */
    char *base = one_round_base(target);
    if (!base) {
      ret = -1;
      goto cleanup;
    }
    const char *ext = tt == FORGE_ASM ? asm_ext : obj_ext;
    for (size_t i = 0; i < n; i++) {
      char *rel = os_path_file_rel(flat[i].file, ext);
      objs[i] = rel ? os_path_join(base, rel) : NULL;
      free(rel);
      if (!objs[i]) {
        free(base);
        ret = -1;
        goto cleanup;
      }
    }
    ret = compile_round(target, flat, (int)n, base, ext, FORGE_SOURCE, tt);
    free(base);
    if (ret != 0)
      goto cleanup;
  } else if (tt == FORGE_SOURCE) {
    /* plain copy: <output_final_path || output_path>/output/<target name>/<rel> */
    char *base = one_round_base(target);
    char *named =
        base ? os_path_join(base, target->target_name
                                      ? target->target_name
                                      : (target->name ? target->name
                                                      : "output"))
             : NULL;
    free(base);
    if (!named) {
      ret = -1;
      goto cleanup;
    }
    for (size_t i = 0; i < n; i++) {
      char *rel = os_path_file_rel(flat[i].file, NULL);
      char *dest = rel ? os_path_join(named, rel) : NULL;
      free(rel);
      if (!dest) {
        ret = -1;
        break;
      }
      ensure_parent_dir(dest);
      if (os_copy_file(flat[i].file, dest) != 0)
        ret = -1;
      free(dest);
      if (ret != 0)
        break;
    }
    free(named);
  }

cleanup:
  for (size_t i = 0; objs && i < n + 1 && objs[i]; i++)
    free(objs[i]);
  free(objs);
  clean_source_array(flat, n);
  return ret;
}
/* ------------------------ Test support (default_test runtime)
 * ------------------------ */

/* strcmp comparator for qsort: array elements are char* (entry strings) */
static int cmp_str(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Recursively collect all regular files under dir (no dirs);
   full relative paths go into a NULL-terminated array sorted by
   strcmp (deterministic); NULL on failure (missing/unreadable dir
   or OOM). The array is plain malloc/realloc'd (same convention
   as os_listdir: the each_file macro frees the array itself and
   frees each element) */
static char **collect_files_recursive(const char *dir) {
  if (!dir || !*dir || !os_path_is_dir(dir))
    return NULL;
  int n = 0;
  char **names = os_listdir(dir, &n);
  if (!names)
    return NULL;

  size_t cap = 16, count = 0;
  char **arr = malloc(cap * sizeof(char *));
  if (!arr) {
    for (int i = 0; i < n; i++)
      free(names[i]);
    free(names);
    return NULL;
  }

  int failed = 0;
  for (int i = 0; i < n && !failed; i++) {
    char *full = os_path_join(dir, names[i]);
    if (!full) {
      failed = 1;
      break;
    }
    if (os_path_is_dir(full)) {
      char **sub = collect_files_recursive(full);
      free(full);
      if (!sub) {
        failed = 1;
        break;
      }
      for (char **p = sub; *p; p++) {
        if (count + 2 > cap) {
          size_t ncap = cap * 2;
          char **na = realloc(arr, ncap * sizeof(char *));
          if (!na) {
            for (char **q = p; *q; q++)
              free(*q); /* elements not yet moved into arr */
            failed = 1;
            break;
          }
          arr = na;
          cap = ncap;
        }
        arr[count++] = *p; /* ownership of the element moves to arr */
      }
      free(sub); /* the array only; elements were moved or freed */
    } else {
      if (count + 2 > cap) {
        size_t ncap = cap * 2;
        char **na = realloc(arr, ncap * sizeof(char *));
        if (!na) {
          failed = 1;
          free(full);
          break;
        }
        arr = na;
        cap = ncap;
      }
      arr[count++] = full;
    }
  }

  for (int i = 0; i < n; i++)
    free(names[i]);
  free(names);

  if (failed) {
    for (size_t i = 0; i < count; i++)
      free(arr[i]);
    free(arr);
    return NULL;
  }
  arr[count] = NULL;
  if (count > 1)
    qsort(arr, count, sizeof(char *), cmp_str);
  return arr;
}

/* List provider for the each_file macro (see build.h); semantics match collect_files_recursive */
char **_list_files(const char *dir) { return collect_files_recursive(dir); }

/* Scopes of a test-file header: non-keyword lines belong to the
   current scope, starting as Source */
typedef enum {
  TEST_SCOPE_SOURCE,   /**< Source: source files (after the test file) */
  TEST_SCOPE_INCLUDE,  /**< Include: header search paths (-I) */
  TEST_SCOPE_LIB_PATH, /**< Library: library search paths (-L) */
  TEST_SCOPE_LINK,     /**< Link: linked library names (-l) */
  TEST_SCOPE_OPTION,   /**< Option: whole-pipeline option entry
                            (TARGET_OPTIONS: compiler + linker + assembler) */
} test_scope_t;

static char *trim_left(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static void trim_right(char *s) {
  size_t len = strlen(s);
  while (len > 0 &&
         (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r'))
    s[--len] = '\0';
}

/* Parse the test-file header and configure target:
   - Only the leading run of `//` lines is processed; parsing stops
     at the first non-`//` line. Iterates by '\n', trimming the
     trailing '\r' too (CRLF-safe);
   - The line content (after `//`) is left-trimmed: if the first
     token hits a keyword (Source / Include / Option / Library /
     Link, case-sensitive) the scope switches and the remainder of
     that line (after trimming) becomes one entry of that scope;
     otherwise the whole line is one entry of the current scope
     (initial scope = Source);
   - Entries apply line by line: Source->_add_sources,
     Include->_add_include_path, Library->_add_lib_path,
     Link->_add_link_lib, Option->TARGET_OPTIONS (whole-pipeline:
     compiler / linker / assembler; the whole entry verbatim, no
     quoting);
   - The test file itself is always the first source added (as in
     the plan.md command examples);
   - Files whose extension is not in {.c,.cpp,.cc,.cxx} are
     silently skipped (no parsing, no error); on a failed file
     read / failed entry application (OOM), returns NULL and
     clears the applied entries (free_target_contents, leaving no
     half-applied config);
   - On success, target->name is replaced by file minus the
     "tests/" (or "tests\\") prefix and the last extension
     (tests/test.c->test, tests/a/test.c->a/test), and the test
     executable location = os_path_join("test", name) is returned,
     with ".exe" appended on Windows; the caller frees it. */
char *parse_test_c(target_t *target, const char *file) {
  if (!target || !file || !*file)
    return NULL;

  /* extension check: skip right away if not in the test-source set */
  const char *dot = strrchr(file, '.');
  if (!dot || (strcmp(dot, ".c") != 0 && strcmp(dot, ".cpp") != 0 &&
               strcmp(dot, ".cc") != 0 && strcmp(dot, ".cxx") != 0))
    return NULL;

  char *content = read_file(file);
  if (!content)
    return NULL;

  int failed = 0;
  if (_add_sources(target, file) != 0)
    failed = 1; /* the test file itself is the first source */

  test_scope_t scope = TEST_SCOPE_SOURCE;
  char *line = content;
  while (line && *line && !failed) {
    char *nl = strchr(line, '\n');
    if (nl)
      *nl = '\0';
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r')
      line[len - 1] = '\0';
    if (!(line[0] == '/' && line[1] == '/'))
      break; /* first non-`//` line: header parsing ends */
    char *p = trim_left(line + 2);

    /* token hit (before the first whitespace): keyword switches the scope */
    char *tok = p;
    while (*tok && *tok != ' ' && *tok != '\t')
      tok++;
    char save = *tok;
    if (save)
      *tok = '\0';
    int is_kw = 1;
    if (strcmp(p, "Source") == 0)
      scope = TEST_SCOPE_SOURCE;
    else if (strcmp(p, "Include") == 0)
      scope = TEST_SCOPE_INCLUDE;
    else if (strcmp(p, "Library") == 0)
      scope = TEST_SCOPE_LIB_PATH;
    else if (strcmp(p, "Link") == 0)
      scope = TEST_SCOPE_LINK;
    else if (strcmp(p, "Option") == 0)
      scope = TEST_SCOPE_OPTION;
    else
      is_kw = 0;
    if (save)
      *tok = save;

    /* entry: the whole line after the keyword (trimmed), or the whole
       line when it has no keyword */
    char *entry = is_kw ? trim_left(tok) : p;
    trim_right(entry);
    if (*entry) {
      switch (scope) {
      case TEST_SCOPE_SOURCE:
        if (_add_sources(target, entry) != 0)
          failed = 1;
        break;
      case TEST_SCOPE_INCLUDE:
        if (_add_include_path(target, entry) != 0)
          failed = 1;
        break;
      case TEST_SCOPE_LIB_PATH:
        if (_add_lib_path(target, entry) != 0)
          failed = 1;
        break;
      case TEST_SCOPE_LINK:
        if (_add_link_lib(target, entry) != 0)
          failed = 1;
        break;
      case TEST_SCOPE_OPTION:
        /* whole-pipeline options (TARGET_OPTIONS): appended to the
           compiler, linker and assembler commands, so flags that must
           reach both compile and final link (e.g. -fsanitize) work
           from the test header alone */
        if (_add(target, LOCATION, TARGET_OPTIONS, 1, entry) != 0)
          failed = 1;
        break;
      }
    }
    line = nl ? nl + 1 : NULL;
  }
  free(content);

  if (failed) {
    /* no free_target_contents call: the target() step is the sole
       release point; returning NULL here lets that step clean up */
    return NULL;
  }

  /* target name: strip the "tests/" prefix and the last extension */
  const char *rel = file;
  if (strncmp(rel, "tests/", 6) == 0 || strncmp(rel, "tests\\", 6) == 0)
    rel += 6;
  size_t rl = strlen(rel);
  size_t stem = rl;
  for (size_t i = rl; i > 0; i--) {
    char c = rel[i - 1];
    if (c == '.') {
      stem = i - 1;
      break;
    }
    if (c == '/' || c == '\\')
      break;
  }
  char *name = malloc(stem + 1);
  if (!name)
    return NULL; /* no free on failure: the target() step cleans up */
  memcpy(name, rel, stem);
  name[stem] = '\0';

  free(target->name); /* naming bookkeeping (the one exception): drop
                         the old name held by new_target */
  target->name = name;

  /* artifact location = os_path_join("test", name) (".exe" appended
     on Windows); the literal "test" must stay in sync with the
     set_output_final_path("test") call inside default_test(); the two
     together decide the artifact path */
  char *tool = os_path_join("test", name);
  if (!tool)
    return NULL;
  /* the executable extension is appended uniformly via os_exe_ext()
     ("" on POSIX / ".exe" on Windows) */
  const char *ext = os_exe_ext();
  if (ext && *ext) {
    char *exe = base_with(tool, NULL, ext);
    if (!exe) {
      free(tool);
      return NULL;
    }
    free(tool);
    tool = exe;
  }
  return tool;
}
