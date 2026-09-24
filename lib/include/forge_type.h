/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file forge_type.h
 * @brief Core data type definitions of the libforge build system.
 *
 * Defines the core structures used by the build DSL runtime: the target
 * description (target_t), source file (source_t), library description
 * (library_t), and compile context (forge_context_t). It is a common
 * dependency of build.h (DSL macros) and forge_compiler.h (compiler
 * command generation).
 *
 * Memory and ownership conventions:
 * - char* and pointer-array fields inside a structure are owned by the
 *   structure itself and freed by its creator (new_target() and the
 *   DSL runtime);
 * - array fields record their element count as "<field>_count" and are
 *   always NULL-terminated for uniform traversal;
 * - the target pointer in forge_context_t is a borrow; the context
 *   itself does not free the target.
 */
#ifndef FORGE_TYPE
#define FORGE_TYPE

#include <stddef.h>
#include <stdint.h>
#include "forge_os.h"
#include "forge_strv.h"
#include "forge_opt.h"

/**
 * @brief Programming language used by the target.
 */
typedef enum {
  ALL,
  C,   /**< the C language */
  CPP, /**< the C++ language */
} target_language_t;

typedef int archiver_type_t;
typedef struct archiver_args_t archiver_args_t;
typedef int assembler_type_t;
typedef struct assembler_args_t assembler_args_t;
typedef int compiler_type_t;
typedef struct compiler_args_t compiler_args_t;
typedef int linker_type_t;
typedef struct linker_args_t linker_args_t;
typedef int symbol_lister_type_t;
typedef struct symbol_lister_args_t symbol_lister_args_t;

/**
 * @brief File / artifact type.
 */
typedef enum {
  FORGE_SOURCE,     /**< source file (.c / .cpp); as a target type, the */
                    /**< sources are copied verbatim, not compiled */
  FORGE_OBJ,        /**< object file (.o / .obj); as a target type, built in */
                    /**< a single pass and exported to output/ */
  FORGE_ASM,        /**< assembly file (.s / .asm); as a target type, built */
                    /**< in a single pass and exported to output/ */
  FORGE_STATIC_LIB, /**< static library (.a / .lib); two-pass compile + link */
  FORGE_SHARED_LIB, /**< shared library (.so / .dylib / .dll); two-pass */
                    /**< compile + link */
  FORGE_EXECUTABLE, /**< executable; two-pass compile + link */
  FORGE_CUSTOM      /**< custom type; _compile returns -1 directly */
} file_type_t;

/**
 * @def HIDDEN symbol visibility: hide (value = 1)
 */
#define HIDDEN 1
/**
 * @def DEFAULT symbol visibility: default (value = 0)
 */
#define DEFAULT 0

/**
 * @def ENABLE boolean flag: enabled (value = 1)
 */
#define ENABLE 1
/**
 * @def DISABLE boolean flag: disabled (value = 0)
 */
#define DISABLE 0

/**
 * @brief Information about a single source file and its compile output.
 *
 * Used for incremental-build decisions: the obj path is no longer stored
 * in source_t (it is passed via ctx.objs / ctx.output_file); the
 * incremental check is done by the compile pass's needs_rebuild(obj, src,
 * cmd), which derives .d/.meta from the obj.
 */
typedef struct source {
  char *file;        /**< source file path (owned by the struct) */
  uint64_t mtime;    /**< last modification time of the source file */
                     /**< (Unix seconds) */
  //char *obj_file;    /**< corresponding object file path, e.g. */
  /**< ".forge/foo.o" (owned by the struct) */
  //uint64_t obj_mtime;/**< last modification time of the object file */
  /**< (Unix seconds) */
  strv_t cflags; /**< per-file compile options */
  struct source *next; /**< next node in the source-file list; NULL at */
                       /**< the tail (owned by the struct) */
} source_t;

/**
 * @brief Description of a third-party library.
 *
 * Introduced via the DSL's add_link_lib(): its header paths are added
 * to the compile search paths and its library paths to the link search
 * paths.
 */
typedef struct {
  char *name;           /**< library name (owned by the struct) */
  strv_t include_paths; /**< header search paths (NULL-terminated array, */
                        /**< owned by the struct) */
  strv_t lib_paths;     /**< library search paths (NULL-terminated array, */
                        /**< owned by the struct) */
} library_t;

library_t *library_new(char* name, int include_count, int lib_count, ...);

typedef struct {
  archiver_type_t archiver;
  char *archiver_path;
  archiver_args_t *archiver_args;
  assembler_type_t assembler;
  char *assembler_path;
  assembler_args_t *assembler_args;
  compiler_type_t compiler;
  char *compiler_path;
  compiler_args_t *compiler_args;
  linker_type_t linker;
  char *linker_path;
  linker_args_t *linker_args;
  symbol_lister_type_t symbol_lister;
  char *symbol_lister_path;
  symbol_lister_args_t *symbol_lister_args;
} toolchain_t;

/**
 * @brief A build target (corresponds to one target(x) block in the DSL).
 *
 * Holds the artifact type, language, compiler, and all compile/link
 * parameters; created by new_target() and freed automatically when the
 * target(x) block ends. All pointer fields are initialized to NULL by
 * new_target() and filled in by the set_* / add_* DSL macros.
 */
typedef struct {
  char *name;                    /**< target name */
  char *target_name;             /**< final artifact name; optional, uses */
                                 /**< name when NULL */
  file_type_t target_type;       /**< artifact type, default FORGE_EXECUTABLE */
  target_language_t language;    /**< programming language, default C */
  toolchain_t toolchain;
  char *output_path;             /**< output root (borrowed): intermediate */
                                 /**< artifacts live under <path>/opt/, */
                                 /**< final exports (including single-pass */
                                 /**< .o/.s and FORGE_SOURCE copies, the */
                                 /**< latter under */
                                 /**< <path>/output/<target-name>/) under */
                                 /**< <path>/output/; the subdirectory names */
                                 /**< cannot be changed through this field */
                                 /**< alone; use output_final_path / */
                                 /**< output_intermediate_path to override */
                                 /**< them entirely */
  char *output_final_path;      /**< final artifact output directory */
                                /**< (borrowed, same semantics as */
                                /**< output_path): when non-NULL, the final */
                                /**< linked/exported artifacts (executable / */
                                /**< library / .o / .s) land there (no longer */
                                /**< via <output_path>/output/); all other */
                                /**< path semantics match output_path; set by */
                                /**< set_output_final_path() */
  char *output_intermediate_path;      /**< intermediate artifact output */
                                       /**< directory (borrowed, same */
                                       /**< semantics as output_path): when */
                                       /**< non-NULL, intermediates go there */
                                       /**< (no longer via the default */
                                       /**< <output_path>/output/; the */
                                       /**< mirroring/flattening naming rules */
                                       /**< are unchanged); all other path */
                                       /**< semantics match output_path; set */
                                       /**< by set_output_intermediate_path() */

  strv_t include_paths; /**< header search paths (NULL-terminated; */
                        /**< passed as -I when compiling) */
  strv_t lib_paths;  /**< library search paths (NULL-terminated; passed */
                     /**< as -L when linking) */
  strv_t link_libs;  /**< libraries to link (NULL-terminated; passed as */
                     /**< -l when linking) */

  library_t **libs;   /**< list of introduced libraries */
  int libs_count;    /**< number of elements in libs */

  strv_t options;    /**< extra compile options (passed to the compiler */
                     /**< verbatim) */

  source_t *sources;     /**< head of the source-file list (in insertion */
                         /**< order, appended by _add_source*; owned by the */
                         /**< struct) */
  source_t *sources_tail;/**< tail of the source-file list (for O(1) */
                         /**< appends; owned by the struct) */

  strv_t exports;      /**< list of symbols to export */
} target_t;

/**
 * @brief Context of a single compile task.
 *
 * Holds everything needed to generate the commands for one pass of one
 * target.
 */
typedef struct {
  target_t *target;   /**< owning target (borrowed, not freed here) */

  source_t *files;   /**< source files participating in the compile pass */
                     /**< (NULL-terminated, borrowed) */
  int files_count;    /**< number of elements in files */

  char **objs;        /**< per-file object path array, parallel to files */
                      /**< (NULL-terminated, borrowed); NULL in the compile */
                      /**< pass, holds the obj paths in the link pass */
                      /**< (replaces the deprecated source_t.obj_file) */
  char *output_file;  /**< output path */

  char *root;         /**< project root (borrowed, not freed here): command */
                      /**< generation uses it to emit paths inside the root */
                      /**< as relative ones (shorter, more readable command */
                      /**< lines; paths outside the root pass through */
                      /**< verbatim); display and .meta comparison use the */
                      /**< same base */

  file_type_t source_type; /**< source file type */
  file_type_t target_type; /**< artifact type */
} forge_context_t;

/**
 * @brief Create a source_t and append it to a target_t
 *
 * @param t the target target_t
 * @param file the target file
 * @return source_t* the created source_t
 */
source_t *source_new(const char* file);
source_t *add_source_node(target_t *t, const char *file);
void source_node_free(source_t *source);
/**
 * @brief Check whether a file name is a source file of the given language
 *
 * @param name the file name
 * @param lang the language
 * @return int 1 if yes, 0 if no
 */
int is_source_ext(const char *name, target_language_t lang);
/**
 * @brief Create and initialize a build target.
 *
 * The returned target_t is allocated with malloc: all pointer fields are
 * initialized to NULL (including the source-file list), and the numeric
 * fields take defaults (target type FORGE_EXECUTABLE, language C,
 * compiler GCC, optimization OPT_NONE, visibility DEFAULT, LTO enabled
 * ENABLE). The DSL's target(x) macro frees it with free() at the end of
 * the block; when calling this function directly, the caller is
 * responsible for freeing.
 *
 * @param[in] name target name (copied internally; NULL allowed).
 * @return pointer to the new target on success, NULL on failure.
 */
target_t *new_target(char *name);

/**
 * @brief Completely free a build target
 *
 * Frees every pointer held inside the target_t. After calling this,
 * free(t) is not needed.
 *
 * @param t pointer to the build target
 */

void free_target_contents(target_t *t);

#endif
