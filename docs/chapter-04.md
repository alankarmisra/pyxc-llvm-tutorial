---
description: "Install LLVM with everything you need: clang, lld, lldb, clangd, and lit — via Homebrew (macOS/Linux), the official installer (Windows), or from source."
---
# 4. Pyxc: Installing LLVM

## Where We Are

The compiler from [Chapter 3](chapter-03.md) can parse Pyxc and report errors with source locations. To turn the AST into machine code, we need LLVM — specifically with the following tools - lld, clangd, lldb, and llvm-lit. On macOS and Linux, Homebrew gets you there in two commands. On Windows, the official LLVM installer does the same. Building compilers is hard enough. We don't need to torture ourselves needlessly. Unless we want to. Consequently, if you're feeling adventurous, you could build from source instead — I did that too just to make sure the instructions are legit. All paths end up in the same place. 

## macOS / Linux — Homebrew

```bash
brew install llvm
brew install lld
```

Homebrew installs LLVM as *keg-only* — it won't replace the system compiler, so you need to add it to your PATH explicitly. Add the following to your shell config (`~/.zshrc`, `~/.bashrc`, or `~/.bash_profile`):

```bash
# LLVM & LLD Toolchain Setup
# brew --prefix resolves the install path regardless of where Homebrew lives
export LLVM_ROOT="$(brew --prefix llvm)"
export LLD_ROOT="$(brew --prefix lld)"

# Add both to PATH
# This ensures clang, lld, lldb, and clangd are all found
export PATH="$LLVM_ROOT/bin:$LLD_ROOT/bin:$PATH"

# Compilation Flags
export LDFLAGS="-L$LLVM_ROOT/lib -L$LLD_ROOT/lib"
export CPPFLAGS="-I$LLVM_ROOT/include -I$LLD_ROOT/include"
export CMAKE_PREFIX_PATH="$LLVM_ROOT;$LLD_ROOT"
```

Then reload your shell:

```bash
source ~/.zshrc   # or ~/.bashrc
```

#### Verify

```bash
clang --version
clangd --version
lldb --version
lld --version
llvm-lit --version
```

The first four should report the same version and point into the Homebrew prefix — run `brew --prefix llvm` and `brew --prefix lld` to confirm the exact paths on your machine.

`llvm-lit` may not be bundled with the Homebrew formula. If the last command fails, install it separately:

```bash
pip install lit
```

After a pip install, the binary is `lit`, not `llvm-lit`. So `lit --version` is what you'd run to verify it.

If that's working, you're done. Skip straight to [Chapter 5](chapter-05.md).

## Windows — Official Installer

Download the latest `LLVM-<version>-win64.exe` from the [LLVM releases page](https://github.com/llvm/llvm-project/releases). Run the installer and when prompted, select **"Add LLVM to the system PATH"**.

Then set the remaining environment variables. Add to your PowerShell profile (`$PROFILE`):

```powershell
# LLVM Toolchain Setup
$env:LLVM_ROOT = "C:\Program Files\LLVM"

# Add to PATH (if the installer didn't already)
$env:PATH = "$env:LLVM_ROOT\bin;$env:PATH"

# Compilation Flags
$env:LDFLAGS = "-L$env:LLVM_ROOT\lib"
$env:CPPFLAGS = "-I$env:LLVM_ROOT\include"
$env:CMAKE_PREFIX_PATH = "$env:LLVM_ROOT"
```

Or set them permanently via **System Properties → Environment Variables**.

#### Verify

```powershell
clang --version
clangd --version
lldb --version
lld --version
llvm-lit --version
```

The first four should report the same version.

`llvm-lit` may not be included in the official installer. If the last command fails, install it separately:

```powershell
pip install lit
```

After a pip install, the binary is `lit`, not `llvm-lit`. So `lit --version` is what you'd run to verify it.

If that's working, you're done. Skip straight to [Chapter 5](chapter-05.md).

## Build from Source

If you'd rather build LLVM yourself — or Homebrew isn't an option for some reason — the result is identical: same tools, same capabilities, just more steps and a longer wait.

### What You'll Install

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
│   └── llvm-lit
├── lib/
└── include/
```

All the tools we need in one place.

### Time and Space Requirements

**Build time:** 30-60 minutes (depends on your machine)
**Disk space:** ~15 GB for the build, ~3 GB for the install

If that's too much, stick with [pre-built LLVM package](https://releases.llvm.org/download.html) for now. You can always build from source later when you need it.

### Prerequisites

You need:
1. A C++ compiler
2. [CMake](https://cmake.org/)
3. [Ninja](https://ninja-build.org/)
4. Python 3 (`llvm-lit` is a Python script)

#### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake and Ninja
brew install cmake ninja
```

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build
```

#### Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build
```

#### Windows

Install:
1. Visual Studio Build Tools (C++ workload)
2. [CMake](https://cmake.org/)
3. [Ninja](https://ninja-build.org/)

Make sure all three are on your `PATH` — CMake and Ninja installers have an option to add themselves; Visual Studio Build Tools does it automatically.

### Optional: Install zstd

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

### Step 1: Clone LLVM

We'll build LLVM 21.1.6 (stable release at time of writing — check the [LLVM releases page](https://github.com/llvm/llvm-project/releases) for newer tags):

```bash
git clone --depth 1 --branch llvmorg-21.1.6 https://github.com/llvm/llvm-project.git
cd llvm-project
```

The `--depth 1` keeps the download small (we don't need full git history).

### Step 2: Configure the Build

Create a build directory:

```bash
mkdir build && cd build
```

Now configure. This tells CMake what to build and where to install it.

#### Linux / macOS

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

#### Windows (PowerShell)

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
- **`LLVM_INCLUDE_TESTS=ON`** - Include `llvm-lit` for testing

If CMake complains about missing dependencies, install them and re-run.

### Step 3: Build

```bash
ninja
```

This takes 30-60 minutes. Go grab coffee.

**If it fails:** Check the error message. Common issues:
- Out of memory → Close other programs, try again
- Missing dependency → Install it (CMake will tell you what)
- Corrupted download → Delete `llvm-project` and re-clone

### Step 4: Install

```bash
ninja install
```

This copies everything to `~/llvm-21-with-clang-lld-lldb/`.

Verify it worked:

```bash
ls ~/llvm-21-with-clang-lld-lldb/bin
```

You should see: `clang`, `clang++`, `lld`, `lldb`, `llvm-config`, `llvm-lit`, and more.

Once the install is done, you can delete the `build/` directory to reclaim the ~15 GB of build artifacts:

```bash
cd .. && rm -rf build
```

### Step 5: Update Your PATH

Add LLVM to your `PATH` so the system finds it first:

#### macOS / Linux (Bash)

Add to `~/.bashrc` or `~/.bash_profile`:

```bash
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

Then reload:
```bash
source ~/.bashrc
```

#### macOS / Linux (Zsh)

Add to `~/.zshrc`:

```zsh
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"
```

Then reload:
```zsh
source ~/.zshrc
```

#### Windows (PowerShell)

Add to your PowerShell profile (`$PROFILE`):

```powershell
$env:PATH = "$HOME\llvm-21-with-clang-lld-lldb\bin;$env:PATH"
```

Or add it permanently via System Environment Variables.

### Step 6: Verify

Check that your shell finds the right LLVM:

#### macOS / Linux

```bash
which clang
# Should show: /Users/yourname/llvm-21-with-clang-lld-lldb/bin/clang

clang --version
llvm-config --version
llvm-lit --version   # named 'lit' if you installed via pip instead
# All three should report the version you built
```

#### Windows (PowerShell)

```powershell
Get-Command clang | Select-Object -ExpandProperty Source
# Should show: C:\Users\yourname\llvm-21-with-clang-lld-lldb\bin\clang.exe

clang --version
llvm-config --version
llvm-lit --version   # named 'lit' if you installed via pip instead
# All three should report the version you built
```

If the version shown doesn't match what you built, your `PATH` isn't set correctly. Fix that before continuing.

### Step 7: Set LLVM_DIR (for CMake)

When building the Pyxc compiler, CMake needs to find LLVM. Tell it where:

#### macOS / Linux

Add to `~/.bashrc` or `~/.zshrc`:

```bash
export LLVM_DIR="$HOME/llvm-21-with-clang-lld-lldb/lib/cmake/llvm"
```

Reload your shell.

#### Windows

Add to your PowerShell profile or Environment Variables:

```powershell
$env:LLVM_DIR = "$HOME\llvm-21-with-clang-lld-lldb\lib\cmake\llvm"
```

### Optional: Configure VS Code

If you're using VS Code, point it to your new `clangd`:

#### Install the clangd Extension

1. Open VS Code
2. Install the "clangd" extension (disable the C/C++ extension if you see conflicts)

#### Configure clangd Path

Add to `.vscode/settings.json` in your project:

**macOS / Linux:**
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

(Adjust the path for your username.)

#### Generate compile_commands.json

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

LLVM is installed. In Chapter 5, we connect it to the parser: walk the AST and emit LLVM IR for the first time. A function definition becomes a real machine-code function.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
