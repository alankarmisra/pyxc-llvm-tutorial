# 3. Building LLVM from Source

## Introduction

This chapter focuses entirely on installing LLVM from source so you can build a real compiler that emits executables. Early chapters can use `brew install llvm`, but once we integrate `lld` (the LLVM linker), we need a source build with the right configuration. We will build LLVM with `clang`, `clang-tools-extra` (for `clangd`), `lld`, `lldb`, and `llvm-lit` enabled.

The commands below install into your home directory (e.g., `~/llvm-21-with-clang-lld-lldb` on Unix-like systems).

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

## Common linker dependency: zstd

Some LLVM builds (especially when `llvm-config --system-libs` includes `-lzstd`) require the zstd development library at link time. If it is missing, you may see an error like:

```text
ld: library 'zstd' not found
```

Install zstd development packages for your platform:

- macOS (Homebrew): `brew install zstd`
- Ubuntu/Debian: `sudo apt install libzstd-dev`
- Fedora: `sudo dnf install libzstd-devel`

The chapter Makefiles support both auto-detection and manual override:

```bash
# If auto-detection fails, set it explicitly:
make ZSTD_LIBDIR="$(brew --prefix zstd)/lib"

# Generic escape hatch for any extra library directory:
make EXTRA_LIBDIR=/custom/lib/path
```

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
  -DLLVM_INCLUDE_TESTS=ON \
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

To make this permanent, add the export line to your shell profile:
- Bash: `~/.bashrc` or `~/.bash_profile`
- Zsh: `~/.zshrc`

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
  -DLLVM_INCLUDE_TESTS=ON \
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
  -DLLVM_INCLUDE_TESTS=ON \
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

This tutorial uses `clangd` for code navigation and diagnostics. The `clangd` language server needs `compile_commands.json` to understand how your code is built. CMake can generate this file automatically.

### Generate compile_commands.json with CMake

When building the tutorial chapters, CMake will create `compile_commands.json` automatically. If you're creating your own project, add this flag to your CMake configuration:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
```

This generates `compile_commands.json` in your build directory. You can then:

**Option 1:** Copy or symlink it to your source root:
```bash
# From your build directory
ln -s $(pwd)/compile_commands.json ../compile_commands.json
```

**Option 2:** Tell clangd where to find it via `.clangd` config file in your source root:
```yaml
CompileFlags:
  CompilationDatabase: build/
```

### Configure VS Code

For this tutorial, we assume you are using VS Code. If you use another editor or IDE, the steps to configure clangd will be slightly different.

1. Install the VS Code extension: `clangd`
2. Open the tutorial repository folder in VS Code
3. The repository already includes `compile_commands.json` at the root
4. Tell VS Code to use the `clangd` you just built by adding this to your settings (adjust the path for your platform):

**macOS/Linux:**
```json
{
  "clangd.path": "/Users/yourname/llvm-21-with-clang-lld-lldb/bin/clangd"
}
```

**Windows:**
```json
{
  "clangd.path": "C:\\Users\\yourname\\llvm-21-with-clang-lld-lldb\\bin\\clangd.exe"
}
```

If IntelliSense conflicts with clangd, disable the built-in C/C++ extension or set `C_Cpp.intelliSenseEngine` to `Disabled` in VS Code settings.

## Need Help?

Building LLVM from source can be challenging, especially with different system configurations. If you hit a snag or have questions:

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report build problems, errors, or bugs
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message
- The command you ran

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!