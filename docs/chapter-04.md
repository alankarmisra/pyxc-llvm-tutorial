---
description: "Build LLVM from source with everything you need: clang, lld, lldb, and clangd."
---
# 4. Pyxc: Building LLVM from Source

## Why Build from Source?

Early chapters can use a pre-built LLVM (`brew install llvm` on macOS). But once we start building real executables and linking, we need:

- **lld** - LLVM's linker
- **clangd** - Language server for IDE support
- **lldb** - LLVM's debugger
- **lit** - Testing framework

Pre-built packages often don't include everything. Building from source gives us full control.

## What You'll Install

By the end of this chapter, you'll have:

```text
~/llvm-21-with-clang-lld-lldb/
├── bin/
│   ├── clang
│   ├── clang++
│   ├── clangd
│   ├── lld
│   ├── lldb
│   ├── llvm-config
│   └── lit
├── lib/
└── include/
```

All the tools we need in one place.

## Time and Space Requirements

**Build time:** 30-60 minutes (depends on your machine)
**Disk space:** ~15 GB for the build, ~3 GB for the install

If that's too much, stick with pre-built LLVM for now. You can always build from source later when you need it.

## Prerequisites

You need:
1. A C++ compiler
2. CMake
3. Ninja (fast build system)

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake and Ninja
brew install cmake ninja
```

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build
```

### Windows

Install:
1. Visual Studio Build Tools (C++ workload)
2. CMake (from cmake.org)
3. Ninja (from ninja-build.org)

Add both to your `PATH`.

## Optional: Install zstd

Some LLVM builds need the `zstd` compression library. If you see `library 'zstd' not found` errors later, install it:

**macOS:**
```bash
brew install zstd
```

**Ubuntu/Debian:**
```bash
sudo apt install libzstd-dev
```

**Fedora:**
```bash
sudo dnf install libzstd-devel
```

You can skip this for now and come back if you hit errors.

## Step 1: Clone LLVM

We'll build LLVM 21.1.6 (latest stable release as of writing):

```bash
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project
```

The `--depth 1` keeps the download small (we don't need full git history).

## Step 2: Configure the Build

Create a build directory:

```bash
mkdir build && cd build
```

Now configure. This tells CMake what to build and where to install it.

### Linux / macOS

```bash
cmake -G Ninja ../llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" \
  -DCMAKE_INSTALL_PREFIX=$HOME/llvm-21-with-clang-lld-lldb \
  -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=ON \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF
```

### Windows (PowerShell)

```powershell
cmake -G Ninja ..\llvm `
  -DCMAKE_BUILD_TYPE=Release `
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" `
  -DCMAKE_INSTALL_PREFIX=$HOME\llvm-21-with-clang-lld-lldb `
  -DLLVM_TARGETS_TO_BUILD="X86" `
  -DLLVM_INCLUDE_EXAMPLES=OFF `
  -DLLVM_INCLUDE_TESTS=ON `
  -DLLVM_INCLUDE_BENCHMARKS=OFF `
  -DLLVM_ENABLE_ASSERTIONS=OFF
```

**What these flags mean:**

- **`-G Ninja`** - Use Ninja (faster than Make)
- **`CMAKE_BUILD_TYPE=Release`** - Optimized build (not debug)
- **`LLVM_ENABLE_PROJECTS`** - Build clang, clangd, lld, and lldb
- **`CMAKE_INSTALL_PREFIX`** - Where to install (your home directory)
- **`LLVM_TARGETS_TO_BUILD`** - Only build for x86 and ARM (speeds up build)
- **`LLVM_INCLUDE_TESTS=ON`** - Include `lit` for testing

If CMake complains about missing dependencies, install them and re-run.

## Step 3: Build

```bash
ninja
```

This takes 30-60 minutes. Go grab coffee.

**If it fails:** Check the error message. Common issues:
- Out of memory → Close other programs, try again
- Missing dependency → Install it (CMake will tell you what)
- Corrupted download → Delete `llvm-project` and re-clone

## Step 4: Install

```bash
ninja install
```

This copies everything to `~/llvm-21-with-clang-lld-lldb/`.

Verify it worked:

```bash
ls ~/llvm-21-with-clang-lld-lldb/bin
```

You should see: `clang`, `clang++`, `lld`, `lldb`, `llvm-config`, `lit`, and more.

## Step 5: Update Your PATH

Add LLVM to your `PATH` so the system finds it first:

### macOS / Linux (Bash)

Add to `~/.bashrc` or `~/.bash_profile`:

```bash
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

Then reload:
```bash
source ~/.bashrc
```

### macOS / Linux (Zsh)

Add to `~/.zshrc`:

```zsh
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

Then reload:
```zsh
source ~/.zshrc
```

### Windows (PowerShell)

Add to your PowerShell profile (`$PROFILE`):

```powershell
$env:PATH = "$HOME\llvm-21-with-clang-lld-lldb\bin;$env:PATH"
```

Or add it permanently via System Environment Variables.

## Step 6: Verify

Check that your shell finds the right LLVM:

```bash
which clang
# Should show: /Users/yourname/llvm-21-with-clang-lld-lldb/bin/clang

clang --version
# Should show: clang version 21.1.6

llvm-config --version
# Should show: 21.1.6
```

If it shows a different version (like system clang), your `PATH` isn't set correctly. Fix that before continuing.

## Step 7: Set LLVM_DIR (for CMake)

When building the Pyxc compiler, CMake needs to find LLVM. Tell it where:

### macOS / Linux

Add to `~/.bashrc` or `~/.zshrc`:

```bash
export LLVM_DIR="$HOME/llvm-21-with-clang-lld-lldb/lib/cmake/llvm"
```

Reload your shell.

### Windows

Add to your PowerShell profile or Environment Variables:

```powershell
$env:LLVM_DIR = "$HOME\llvm-21-with-clang-lld-lldb\lib\cmake\llvm"
```

## Optional: Configure VS Code

If you're using VS Code, point it to your new `clangd`:

### Install the clangd Extension

1. Open VS Code
2. Install the "clangd" extension (disable C/C++ extension to avoid conflicts)

### Configure clangd Path

Add to `.vscode/settings.json` in your project:

```json
{
  "clangd.path": "/Users/yourname/llvm-21-with-clang-lld-lldb/bin/clangd"
}
```

(Adjust the path for your username.)

### Generate compile_commands.json

When you build Pyxc, CMake will generate `compile_commands.json`. This tells clangd how to compile your code.

In your project's CMakeLists.txt, add:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

After building, you'll see `build/compile_commands.json`. clangd reads this automatically.

## Troubleshooting

### Build fails with "out of memory"

Ninja uses all CPU cores by default. Limit it:

```bash
ninja -j4  # Use 4 cores instead of all
```

### Can't find zstd library

Install zstd (see "Optional: Install zstd" above), then rebuild.

Or tell CMake to skip it:

```bash
cmake -G Ninja ../llvm \
  -DLLVM_ENABLE_ZSTD=OFF \
  # ... other flags
```

### Wrong clang version still showing

Check:

```bash
echo $PATH
```

Make sure your LLVM `bin` directory comes BEFORE `/usr/bin` or other system paths.

### Windows: Ninja not found

Make sure Ninja is on your `PATH`:

```powershell
ninja --version
```

If that fails, download Ninja and add its directory to `PATH` in System Environment Variables.

## What's Next

You now have a full LLVM toolchain. In the next chapters, we'll use:
- **clang** to compile C code
- **lld** to link executables
- **llvm-config** to find LLVM libraries
- **lit** to run tests

Back to building the language!

## Summary

You installed:
- LLVM 21.1.6 with clang, clangd, lld, lldb
- Updated `PATH` and `LLVM_DIR`
- (Optional) Configured VS Code with clangd

Total time: ~1 hour (mostly waiting for the build).

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
