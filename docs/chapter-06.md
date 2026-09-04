---
section: "LLVM and Execution"
description: "Install LLVM and verify that pyxc's CMake project can discover it."
---

# 6. pyxc: Installing LLVM

Next: connect the working pyxc frontend to LLVM.

Do not add code generation yet. This chapter has one smaller boundary:

```text
installed LLVM -> find_package(LLVM) succeeds
```

Chapter 7 depends on LLVM headers and libraries. If discovery is unreliable now, every later build error will be harder to diagnose.

Work in:

```bash
cd code/chapter-06
```

## 1. Make CMake Require LLVM

The C++ source stays the same as Chapter 5. In `CMakeLists.txt`, add:

```cmake
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
```

`CONFIG` tells CMake to find LLVM's installed `LLVMConfig.cmake`. The two status messages make the selected version and installation path visible during every configure.

If LLVM is already installed, try the verification immediately:

```bash
cmake -S . -B build
```

Expected shape:

```text
-- Found LLVM <version>
-- Using LLVMConfig.cmake in: <path>
-- Configuring done
```

If that works, build and run the unchanged frontend:

```bash
cmake --build build
./build/pyxc
```

Otherwise, choose one installation path below.

## 2. Install with Homebrew on macOS or Linux

Install LLVM and the separately packaged linker/debugger tools:

```bash
brew install llvm lld lldb
```

Homebrew keeps LLVM separate from the system toolchain. Ask Homebrew for the prefix rather than hard-coding `/opt/homebrew` or `/usr/local`:

```bash
brew --prefix llvm
brew --prefix lld
brew --prefix lldb
```

For the current shell, export a project-specific LLVM location:

```bash
export PYXC_LLVM_ROOT="$(brew --prefix llvm)"
export PATH="$PYXC_LLVM_ROOT/bin:$(brew --prefix lld)/bin:$(brew --prefix lldb)/bin:$PATH"
```

Then configure pyxc explicitly:

```bash
cmake -S . -B build \
  -DLLVM_DIR="$PYXC_LLVM_ROOT/lib/cmake/llvm"
cmake --build build
```

Using `LLVM_DIR` is more precise than hoping CMake searches the correct prefix. It points directly to the directory containing `LLVMConfig.cmake`.

Verify the tools:

```bash
llvm-config --version
clang --version
lld --version
lldb --version
```

Homebrew may not provide a convenient `llvm-lit` command. If neither `llvm-lit` nor `lit` exists, install the Python package:

```bash
python3 -m pip install lit
lit --version
```

## 3. Install a Released Build on Windows

Download the current Windows release installer from the official [LLVM releases page](https://github.com/llvm/llvm-project/releases).

During installation, allow the installer to add LLVM to `PATH`, or add its `bin` directory afterward. A typical installation prefix is:

```text
C:\Program Files\LLVM
```

Open a new PowerShell session and verify:

```powershell
clang --version
lld --version
lldb --version
```

Configure pyxc with the LLVM CMake package directory:

```powershell
cmake -S . -B build `
  -DLLVM_DIR="C:\Program Files\LLVM\lib\cmake\llvm"
cmake --build build
```

If `lit` is missing:

```powershell
py -m pip install lit
lit --version
```

If the release package does not contain the development CMake files required by `find_package`, use a package manager that provides LLVM development files or build LLVM from source.

## 4. Build LLVM from Source

Use this route when you want a specific revision, need all development files, or plan to work on LLVM itself.

Install the prerequisites first:

```text
CMake 3.20 or newer
Ninja
Python 3.8 or newer
a working C and C++ compiler
Git
```

Clone the monorepo:

```bash
git clone --depth 1 https://github.com/llvm/llvm-project.git
cd llvm-project
```

Choose explicit build and installation directories. Replace the example install prefix with a location you own:

```bash
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" \
  -DLLVM_INSTALL_UTILS=ON
```

Build:

```bash
cmake --build build
```

This can consume substantial time, memory, and disk space. If linking exhausts memory, reconfigure with a smaller link-job limit:

```bash
-DLLVM_PARALLEL_LINK_JOBS=1
```

Optionally run LLVM's tests:

```bash
cmake --build build --target check-all
```

Install into the chosen prefix:

```bash
cmake --build build --target install
```

Then configure pyxc against it:

```bash
cd /path/to/pyxc-llvm-tutorial/code/chapter-06
cmake -S . -B build \
  -DLLVM_DIR=/path/to/llvm-project/install/lib/cmake/llvm
cmake --build build
```

You may also configure pyxc directly against an LLVM build tree:

```bash
cmake -S . -B build \
  -DLLVM_DIR=/path/to/llvm-project/build/lib/cmake/llvm
```

## 5. Diagnose CMake Discovery Failures

If CMake reports:

```text
Could not find a package configuration file provided by "LLVM"
```

find the configuration file:

```bash
find /path/to/llvm -name LLVMConfig.cmake
```

Then pass its containing directory—not the file itself—as `LLVM_DIR`:

```bash
cmake -S . -B build -DLLVM_DIR=/path/containing/LLVMConfig.cmake
```

If CMake cached a wrong LLVM installation, use a new build directory:

```bash
cmake -S . -B build-llvm \
  -DLLVM_DIR=/correct/path/lib/cmake/llvm
cmake --build build-llvm
```

This avoids mixing configuration from two LLVM versions.

## 6. Verify the Complete Boundary

Run:

```bash
llvm-config --version
llvm-config --cmakedir
```

The second command should identify the directory you pass as `LLVM_DIR`.

Then configure, build, and test:

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
llvm-lit -v test/  # use `lit -v test/` if installed from pip
```

The chapter source still behaves like Chapter 5. The new result is entirely in the build system:

```text
CMake found LLVM headers, definitions, version, and package configuration
```

Useful official references:

- [Getting Started with the LLVM System](https://llvm.org/docs/GettingStarted.html)
- [Building LLVM with CMake](https://llvm.org/docs/CMake.html)
- [LLVM releases](https://github.com/llvm/llvm-project/releases)
- [Homebrew LLVM formula](https://formulae.brew.sh/formula/llvm)

Next: [Chapter 7](chapter-07.md) gives every AST node a `codegen()` method and emits LLVM IR.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later
