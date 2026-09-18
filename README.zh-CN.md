<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception -->
# Forge

自举的构建系统：C 语言,两工具链 GCC / Clang
(Clang 复用 GCC 兼容参数子集)，
当前仅支持 Linux，架构上为跨平台预留了 os 层。
构建脚本本身就是一个普通的 C 源文件
(`build.c`),由宿主工具 forge 编译成可执行文件后执行--"构建系统编译
构建脚本,再运行它来构建自己"。


## 快速开始

```sh
./bootstrap.sh        # 从零自举:宿主 forge + 运行时库 + 全量构建 + 测试
./make                # 增量构建(等同 forge . 生成的 make 再次运行)
./make test           # 运行测试套件
./make install /usr/local     # 安装到指定 prefix(Linux)
./make gen_deb        # 生成 .deb 安装包(Linux)
```

`./bootstrap.sh` 是全流程唯一需要手工的地方:它用系统编译器直编宿主
工具、手工打包第一版运行时库,之后一切交给自举出的 `./make`。
详见 `bootstrap.sh` 头注释。

### 构建脚本示例

一个项目的构建脚本就是一个 `build.c`:

```c
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

把文件放进工程根目录(带一个 `build.conf`),然后:

```sh
forge .        # 编译 build.c 生成 ./make
./make         # 首次全量构建;此后按 .d/.meta 增量
```

产物输出在项目根 `output/`(缺省输出目录，可 `set_output_path()`
修改)，本例为 `output/app`。

`build.conf` 决定 forge 本体如何生成 `./make`--也就是**用哪个编译器
编译 build.c 脚本本身**(以及链接 make 时的库),与项目目标的工具链无
关。目标用什么编译器,在 build.c 里由每个 `target()` 块内的
`set_toolchain()` 决定,两者互不影响:

```
compiler=GCC
compiler_path=/usr/bin/gcc
```

支持按平台加后缀覆盖(`compiler_windows=…`、`compiler_linux=…`、
`compiler_mac_os=…`),缺省值平台的同名键兜底。所有配置项会以
`-DCONF_<KEY>=<VALUE>` 的形式,作为编译 build.c 时的宏注入(与
FORGE_VERSION 同一传递机制;编译经 execv 直达、无 shell 层,值不
包裹引号,首尾空白剔除,键名按 conf 原样加 `CONF_` 前缀)。build.c
里期望字符串值时自行字符串化,如:

```c
#define STR(tok) #tok
if (strcmp(STR(CONF_compiler), "GCC") == 0) { /* … */ }
```

`#` 起注释(整行或行尾均可,剔除至行末)。

`forge` 来自仓库自举产物(`build/output/forge`)。未安装到系统时,
给它一个含头文件与运行时库的前缀即可对任意项目使用;也可以先安装,
之后直接 `forge .`:

```sh
FORGE_PREFIX=$PWD/build/bootstrap-prefix build/output/forge 项目目录
# 或:./make install /usr/local 后直接 forge 项目目录
```

生成的脚本可执行文件名可由 `output_name` 指定(缺省 `make`,必须是
不含路径分隔符的纯文件名):

```
output_name=mybuild     # 生成 ./mybuild(此例为行内注释)
```

完整 DSL 面(全部 `set`/`add` 选项、目标类型、语言、优化级别)见
`lib/include/build.h`。

每个 `function(build)` 都是可执行文件里的一个函数,也能以子命令形式
单独跑:

```sh
./make install /usr/local    # 等价于调用 function_install(argc, argv)
./make test                  # function_test
```

## 设计思路

**把构建脚本编译成程序，而不是解释它。**
`build.c` 是真实可编译的 C 源码。`forge` 对它做编译与链接，产物是一个可执行文件，其中包含用户的全部 `function()`。运行时按名字查找 `function_<name>` 并调用，所以每个 function 天然是一条子命令，参数就是 `argc/argv`。

**宏收集配置，函数执行动作。**
`set`/`add` 系列宏只把配置写进 `target_t`，`compile()` 才真正干活：增量判定、并行编译、按目标类型选后端。宏是声明层，`compile()` 是执行层，两层分开。

**平台差异收敛在 os_ 层。**
平台相关能力（进程、管道、路径、时间、线程）声明在 `forge_os.h`，各平台分别实现。命令生成按工具链后端分工（compiler / linker / archiver）。跨平台语义（引号、退出码）有统一契约。

**构建产物**
Forge 自举产出三类产物，全部位于 `build/` 下：

| 产物 | 内容 | 用途 |
|------|------|------|
| `build/output/forge` | 宿主工具(src/ + lib/src/os) | 把任意 `build.c` 编译成 `make`|
| `build/output/libmain.a` / `build/shared/output/libforge.so` | 运行时库(lib/src) | make 的链接原料与共享版 |
| `./make` | 构建脚本对象 + 运行时库 | 执行本项目的构建函数 |

**测试即小程序。**
`default_test()` 一行启用整个测试套件：递归扫描 `tests/`，每个 `tests/*.c` 通过头注释自声明依赖与编译参数，各自编译成独立程序运行，`TEST()`/`PASS()` 断言。加一个 `tests/xxx.c` 即新增一条测试。

## 目录结构

```
build.c         本项目的构建脚本，也是 DSL 的参考用法
bootstrap.sh    从零自举脚本
src/            宿主工具(forge / cli / parser / generate)
include/        宿主工具头文件
lib/src/        运行时库实现(os / compiler / linker / archiver / …)
lib/include/    运行时库公开头(forge_os.h / build.h / forge_type.h / …)
tests/          测试源(// Source 自声明依赖)
build/          生成物（全部 gitignore）
make            本项目的本地构建入口（gitignore）
test/           测试生成物（gitignore）
.forge/         增量缓存（gitignore）
```

## 许可

Forge 本体、运行时库、文档与示例均采用 GPLv3+(完整条款见 `LICENSE`,
含 GPLv3+ §7 附加许可)。

作为例外，你可以把 Forge 的部分源码直接复制进构建脚本，按需修改。
这里的“构建脚本”指 Forge 用作项目构建定义或构建入口的文件，
无论其名称、扩展名或位置，例如默认的 `build.c`。仅因这些复制部分，
该构建脚本及其构建产物无需按 GPLv3+ 授权或公开源码，也无需为
被复制部分署名。条件是：不得虚假标注被复制部分的作者，也不得对
被复制部分主张独占版权。

这些被复制进来的代码由你自行维护。内部符号可能在新版本中变化。

生成的 `make`(构建脚本对象 + 运行时库)可以按 GPLv3+ 条款二次分发,
但不推荐:它绑定你的项目与 Forge 版本,单独分发价值有限。不能把
Forge 当通用库嵌进无关应用--附加许可只针对构建脚本及其构建产物,
整库嵌入仍受 GPLv3+ 约束(见 LICENSE)。

## 当前不支持

- Windows / MSVC(os 层与编译器后端为桩,未实测禁用;链接到此平台即失败)
- macOS 兼容性保证(同上,os 层为桩)
- 包管理
- 交叉编译矩阵
- IDE 工程生成

这些不是 bug，是当前边界。欢迎提 issue 讨论优先级。