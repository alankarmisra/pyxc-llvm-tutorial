# 3. Building LLVM from Source

## Introduction

This chapter focuses entirely on installing LLVM from source so you can build a real compiler that emits executables. Early chapters can use `brew install llvm`, but once we integrate `lld` (the LLVM linker), we need a source build with the right configuration. We will build LLVM with `clang`, `clang-tools-extra` (for `clangd`), `lld`, and `lldb` enabled.

The commands below install into `~/llvm-21-with-clang-lld-lldb`.

## Prerequisites

You need a C/C++ compiler, CMake, and Ninja. These are not always installed by default.

- macOS (Apple Silicon / Intel):
  - Install Xcode Command Line Tools:
    - `xcode-select --install`
  - Install CMake and Ninja:
    - Homebrew: `brew install cmake ninja`
    - MacPorts: `sudo port install cmake ninja`
    - Manual install:
      - CMake: download and install from the official CMake website, then ensure `cmake` is on your `PATH`.
      - Ninja: download the Ninja binary, place it in a directory on your `PATH`, and make it executable.
- Ubuntu/Debian:
  - `sudo apt update`
  - `sudo apt install build-essential cmake ninja-build`
- Fedora:
  - `sudo dnf install gcc-c++ cmake ninja-build`
- Windows (x64):
  - Install Visual Studio Build Tools (C++ workload).
  - Install CMake and Ninja and ensure they are on `PATH`.

## Build LLVM 21.1.6 (latest stable)

### Linux 64 (x86_64)

```bash
# Clone LLVM 21.1.6 (latest stable)
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project

# Create build directory
mkdir build && cd build

# Configure the build
cmake -G Ninja ../llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" \
  -DCMAKE_INSTALL_PREFIX=$HOME/llvm-21-with-clang-lld-lldb \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF

# Build (this will take 30-60 minutes on a typical machine)
ninja

# Install
ninja install
```

After install, add the new LLVM tools to your `PATH`:

```bash
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

If you are building other projects with CMake that need this LLVM, you can also set:

```bash
export LLVM_DIR="$HOME/llvm-21-with-clang-lld-lldb/lib/cmake/llvm"
```

### Mac Silicon (Apple M1/M2/M3)

```bash
# Clone LLVM 21.1.6 (latest stable)
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project

# Create build directory
mkdir build && cd build

# Configure the build
cmake -G Ninja ../llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" \
  -DCMAKE_INSTALL_PREFIX=$HOME/llvm-21-with-clang-lld-lldb \
  -DLLVM_TARGETS_TO_BUILD="AArch64" \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF

# Build (this will take 30-60 minutes on M1/M2 Mac)
ninja

# Install
ninja install
```

After install, add the new LLVM tools to your `PATH`:

```bash
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

If you are building other projects with CMake that need this LLVM, you can also set:

```bash
export LLVM_DIR="$HOME/llvm-21-with-clang-lld-lldb/lib/cmake/llvm"
```

### Mac Intel (x86_64)

```bash
# Clone LLVM 21.1.6 (latest stable)
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project

# Create build directory
mkdir build && cd build

# Configure the build
cmake -G Ninja ../llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" \
  -DCMAKE_INSTALL_PREFIX=$HOME/llvm-21-with-clang-lld-lldb \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF

# Build (this will take 30-60 minutes on Intel Mac)
ninja

# Install
ninja install
```

After install, add the new LLVM tools to your `PATH`:

```bash
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

If you are building other projects with CMake that need this LLVM, you can also set:

```bash
export LLVM_DIR="$HOME/llvm-21-with-clang-lld-lldb/lib/cmake/llvm"
```

### Windows 64 (x86_64, PowerShell)

```powershell
# Clone LLVM 21.1.6 (latest stable)
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project

# Create build directory
mkdir build
cd build

# Configure the build
cmake -G Ninja ..\llvm -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" -DCMAKE_INSTALL_PREFIX="$env:USERPROFILE\llvm-21-with-clang-lld-lldb" -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=ON -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_ASSERTIONS=OFF

# Build (this will take 30-60 minutes on a typical machine)
ninja

# Install
ninja install
```

After install, add the new LLVM tools to your `PATH`:

```powershell
$env:Path = "$env:USERPROFILE\llvm-21-with-clang-lld-lldb\bin;$env:Path"
```

If you are building other projects with CMake that need this LLVM, you can also set:

```powershell
$env:LLVM_DIR = "$env:USERPROFILE\llvm-21-with-clang-lld-lldb\lib\cmake\llvm"
```

## VS Code: clangd + compile_commands.json

This tutorial includes `compile_commands.json`, which lets `clangd` provide accurate code navigation and diagnostics. `clangd` is built by enabling `clang-tools-extra` in the LLVM build above.

For the purposes of this tutorial, we assume you are using VS Code. If you use another editor or IDE, the steps to point it at your LLVM/clangd install will be a little different.

### Configure VS Code

1. Install the VS Code extension: `clangd`.
2. Open the repository folder in VS Code.
3. Ensure `compile_commands.json` is at the repository root (it already is in this tutorial).
4. Tell VS Code to use the `clangd` you just built by adding this to your settings (edit the path for your platform):

```json
"clangd.path": "~/llvm-21-with-clang-lld-lldb/bin/clangd"
```

If IntelliSense conflicts with clangd, disable the built-in C/C++ extension or set `C_Cpp.intelliSenseEngine` to `Disabled` in VS Code settings.

### Example compile_commands.json

If you need to recreate or edit `compile_commands.json`, use this as a starting point and update the paths for your install and repo location:

```json
{
  "directory": "~/path/to/your/repo",
  "arguments": [
    "~/llvm-21-with-clang-lld-lldb/bin/clang++",
    "-g",
    "-O3",
    "-I~/llvm-21-with-clang-lld-lldb/include",
    "-Iinclude",
    "-std=c++17",
    "-stdlib=libc++",
    "-fno-exceptions",
    "-funwind-tables",
    "pyxc.cpp",
    "-L~/llvm-21-with-clang-lld-lldb/lib",
    "-lLLVM-21",
    "-llldCommon",
    "-llldELF",
    "-llldMachO",
    "-llldCOFF",
    "-o",
    "pyxc"
  ],
  "file": "pyxc.cpp"
}
```
