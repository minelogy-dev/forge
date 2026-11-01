# LTO and linker parallelism

The public contract is `set(TARGET_LTO, 1, ENABLE)` or `DISABLE`, not a shared
set of toolchain-specific LTO modes. `ENABLE` selects GCC's native LTO on GCC
and ThinLTO on Clang. LTO is enabled by default. The existing `COMPILER_LTO`
and `LINKER_LTO` switches still control the individual phases when needed.

This changes Clang targets from the driver's implicit Full LTO to explicit
ThinLTO at both compilation and link time. Clean old output directories when
moving between compiler families or compiler versions; their LTO objects are
not interchangeable. Re-bootstrap Forge after updating its headers/runtime.

## Job budget on Linux

`./make -j N` sets a stage's worker budget, including `-j 1`. File compilation
and final linking run in separate stages; this is not a process-wide thread
or memory limit. Each backend translates the budget into its supported flags:

| Backend | Compile option | Link options |
| --- | --- | --- |
| GCC, default linker | `-flto` | `-flto=N` when N > 1; otherwise `-flto` |
| Clang, default GNU linker with LLVM plugin | `-flto=thin` | `-flto=thin -Wl,-plugin-opt,jobs=N` |
| Clang, explicitly selected ELF LLD | `-flto=thin` | `-flto=thin -Wl,--thinlto-jobs=N -Wl,--threads=N` |

The Clang default-linker route needs the matching LLVM linker plugin
(`LLVMgold.so`). The ELF LLD route handles LLVM bitcode directly and needs no
GNU linker plugin. A missing linker or plugin fails the build; Forge does not
silently disable LTO or switch linkers.

`DISABLE` removes the generated LTO and LTO-job options. Explicit ELF LLD still
receives `--threads=N` because its ordinary linker workers also use the budget.
The generated Linux linker flags are not promised for other object formats;
macOS and Windows backends remain disabled in this release.

## Select Clang and LLD

Inside a target block:

```c
set_toolchain(CLANG);
set(TARGET_LINKER, 1, LLD);
set(TARGET_LTO, 1, ENABLE);
```

`TARGET_LINKER = LLD` now actually selects LLD via the Clang driver using
`-fuse-ld=lld`. To select a specific installed version, also set:

```c
set(TARGET_LINKER_PATH, 1, "/usr/bin/ld.lld-19");
```

Clang receives the path as one `--ld-path=...` argument, including any spaces.
The selected path must match the configured linker family; Forge does not
guess capabilities from executable names. Without `LLD`, the Clang driver
keeps its default linker and uses the LLVM plugin's ThinLTO job option.

Use a matching Clang/LLVM/LLD installation. CI uses GCC 14 and Clang/LLVM/LLD 19,
and requires real multi-module links through both the default GNU linker and
LLD, executable execution, LTO-disabled builds and relinking after `-j` changes.

Tool-specific overrides still belong in `TARGET_OPTIONS` / `LINKER_OPTIONS`,
which are appended after generated options. Their compatibility and any
additional parallelism controls belong to that explicit configuration.
