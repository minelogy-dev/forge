/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file type.c
 * @brief Implementation of the forge_type.h data types: new_target /
 *        free_target_contents / the source series / library_new.
 *
 * Lifetime convention: target_t is created by the target() macro and
 * fully released via free_target_contents at each loop step (the
 * sole release point); _compile and parse_test_c do not free the
 * target. Every strv member must be strv_init'ed before use (append
 * on a zero-initialized strv depends on the implementation, see
 * strv.c).
 */
#include "forge_type.h"
#include "forge_os.h"
#include "forge_def.h"
#include "forge_archiver.h"
#include "forge_assembler.h"
#include "forge_compiler.h"
#include "forge_linker.h"
#include "forge_symbol_lister.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------ source file nodes ------------------------ */

int is_source_ext(const char *name, target_language_t lang) {
  if(lang == ALL) return 1;
  const char *dot = strrchr(name, '.');
  if (!dot)
    return 0;
  if (lang == CPP)
    return strcmp(dot, ".c") == 0 || strcmp(dot, ".cpp") == 0 ||
           strcmp(dot, ".cc") == 0 || strcmp(dot, ".cxx") == 0;
  return strcmp(dot, ".c") == 0;
}

source_t *source_new(const char *file) {
  source_t *s = calloc(1, sizeof(source_t));
  if (!s)
    return NULL;
  s->file = forge_strdup(file);
  if (!s->file) {
    free(s);
    return NULL;
  }
  if (strv_init(&s->cflags) != 0) {
    free(s->file);
    free(s);
    return NULL;
  }
  return s;
}

source_t *add_source_node(target_t *t, const char *file) {
  source_t *s = source_new(file);
  if (!s)
    return NULL;
  if (t->sources_tail)
    t->sources_tail->next = s;
  else
    t->sources = s;
  t->sources_tail = s;
  return s;
}

void source_node_free(source_t *source) {
  if (!source)
    return;
  free(source->file);
  strv_destroy(&source->cflags);
  free(source);
}

/* ----------------------- the five args structs ----------------------- */

/* Allocate each args struct + init its internal strvs; on failure
   returns NULL (the caller does partial cleanup via
   free_target_contents; unallocated strvs are calloc zeroes and
   strv_destroy is safe on strs==NULL) */
static compiler_args_t *new_compiler_args(toolchain_t *tc) {
  compiler_args_t *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  if (strv_init(&a->defines) != 0 || strv_init(&a->options) != 0) {
    free(a);
    return NULL;
  }
  a->toolchain = tc;
  a->optimization = OPT_NONE;
  a->visibility = DEFAULT;
  a->lto_enabled = ENABLE;
  return a;
}

static linker_args_t *new_linker_args(toolchain_t *tc) {
  linker_args_t *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  if (strv_init(&a->lib_search_paths) != 0 ||
      strv_init(&a->libraries) != 0 || strv_init(&a->export_symbol) != 0 ||
      strv_init(&a->options) != 0) {
    free(a);
    return NULL;
  }
  a->toolchain = tc;
  a->lto_enabled = ENABLE;
  return a;
}

static archiver_args_t *new_archiver_args(toolchain_t *tc) {
  archiver_args_t *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  if (strv_init(&a->options) != 0) {
    free(a);
    return NULL;
  }
  a->toolchain = tc;
  return a;
}

static assembler_args_t *new_assembler_args(toolchain_t *tc) {
  assembler_args_t *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  if (strv_init(&a->options) != 0) {
    free(a);
    return NULL;
  }
  a->toolchain = tc;
  return a;
}

static symbol_lister_args_t *new_symbol_lister_args(toolchain_t *tc) {
  symbol_lister_args_t *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  if (strv_init(&a->options) != 0) {
    free(a);
    return NULL;
  }
  a->toolchain = tc;
  return a;
}

/* --------------------- target creation / release --------------------- */

target_t *new_target(char *name) {
  target_t *t = calloc(1, sizeof(target_t));
  if (!t)
    return NULL;
  if (name) {
    t->name = forge_strdup(name);
    if (!t->name) {
      free(t);
      return NULL;
    }
  }
  t->target_type = FORGE_EXECUTABLE;
  t->language = C;
  /* Toolchain family default: the GCC family (the MSVC family is
     corrected by the TARGET_COMPILER case in _set, which overrides
     it per compiler) */
  t->toolchain.compiler = GCC;
  t->toolchain.archiver = AR;
  t->toolchain.assembler = GCC;
  t->toolchain.linker = LD;
  t->toolchain.symbol_lister = NM;

  /* target-level strvs: _add appends directly, so init comes first */
  if (strv_init(&t->include_paths) != 0 || strv_init(&t->lib_paths) != 0 ||
      strv_init(&t->link_libs) != 0 || strv_init(&t->options) != 0 ||
      strv_init(&t->exports) != 0) {
    free_target_contents(t);
    return NULL;
  }

  t->toolchain.compiler_args = new_compiler_args(&t->toolchain);
  t->toolchain.linker_args = new_linker_args(&t->toolchain);
  t->toolchain.archiver_args = new_archiver_args(&t->toolchain);
  t->toolchain.assembler_args = new_assembler_args(&t->toolchain);
  t->toolchain.symbol_lister_args = new_symbol_lister_args(&t->toolchain);
  if (!t->toolchain.compiler_args || !t->toolchain.linker_args ||
      !t->toolchain.archiver_args || !t->toolchain.assembler_args ||
      !t->toolchain.symbol_lister_args) {
    free_target_contents(t);
    return NULL;
  }
  return t;
}

void free_target_contents(target_t *t) {
  if (!t)
    return;

  free(t->name);
  free(t->target_name);
  free(t->output_path);
  free(t->output_final_path);
  free(t->output_intermediate_path);

  /* the toolchain's five paths */
  free(t->toolchain.archiver_path);
  free(t->toolchain.assembler_path);
  free(t->toolchain.compiler_path);
  free(t->toolchain.linker_path);
  free(t->toolchain.symbol_lister_path);

  /* the five args structs (members first, then the struct itself) */
  if (t->toolchain.compiler_args) {
    free(t->toolchain.compiler_args->standard);
    strv_destroy(&t->toolchain.compiler_args->defines);
    strv_destroy(&t->toolchain.compiler_args->options);
    free(t->toolchain.compiler_args);
  }
  if (t->toolchain.linker_args) {
    free(t->toolchain.linker_args->map_file);
    strv_destroy(&t->toolchain.linker_args->lib_search_paths);
    strv_destroy(&t->toolchain.linker_args->libraries);
    strv_destroy(&t->toolchain.linker_args->export_symbol);
    strv_destroy(&t->toolchain.linker_args->options);
    free(t->toolchain.linker_args);
  }
  if (t->toolchain.archiver_args) {
    strv_destroy(&t->toolchain.archiver_args->options);
    free(t->toolchain.archiver_args);
  }
  if (t->toolchain.assembler_args) {
    strv_destroy(&t->toolchain.assembler_args->options);
    free(t->toolchain.assembler_args);
  }
  if (t->toolchain.symbol_lister_args) {
    free(t->toolchain.symbol_lister_args->format);
    strv_destroy(&t->toolchain.symbol_lister_args->options);
    free(t->toolchain.symbol_lister_args);
  }

  strv_destroy(&t->include_paths);
  strv_destroy(&t->lib_paths);
  strv_destroy(&t->link_libs);
  strv_destroy(&t->options);
  strv_destroy(&t->exports);

  if (t->libs) {
    for (int i = 0; i < t->libs_count; i++) {
      if (!t->libs[i])
        continue;
      free(t->libs[i]->name);
      strv_destroy(&t->libs[i]->include_paths);
      strv_destroy(&t->libs[i]->lib_paths);
      free(t->libs[i]);
    }
    free(t->libs);
  }

  for (source_t *s = t->sources; s;) {
    source_t *nx = s->next;
    source_node_free(s);
    s = nx;
  }

  free(t);
}

/* ------------------------ library description ------------------------ */

library_t *library_new(char *name, int include_count, int lib_count, ...) {
  if (!name)
    return NULL;
  library_t *lib = calloc(1, sizeof(*lib));
  if (!lib)
    return NULL;
  lib->name = forge_strdup(name);
  if (!lib->name) {
    free(lib);
    return NULL;
  }
  if (strv_init(&lib->include_paths) != 0 || strv_init(&lib->lib_paths) != 0) {
    free(lib->name);
    free(lib);
    return NULL;
  }

  va_list ap;
  va_start(ap, lib_count);
  for (int i = 0; i < include_count; i++) {
    const char *p = va_arg(ap, const char *);
    if (strv_append(&lib->include_paths, p) != 0) {
      va_end(ap);
      free(lib->name);
      strv_destroy(&lib->include_paths);
      strv_destroy(&lib->lib_paths);
      free(lib);
      return NULL;
    }
  }
  for (int i = 0; i < lib_count; i++) {
    const char *p = va_arg(ap, const char *);
    if (strv_append(&lib->lib_paths, p) != 0) {
      va_end(ap);
      free(lib->name);
      strv_destroy(&lib->include_paths);
      strv_destroy(&lib->lib_paths);
      free(lib);
      return NULL;
    }
  }
  va_end(ap);
  return lib;
}