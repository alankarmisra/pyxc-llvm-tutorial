# Building on macOS

This guide covers building the self-hosting project on macOS with system clang++ or Homebrew LLVM.

## Prerequisites

### 1. Xcode Command Line Tools
```bash
xcode-select --install
```

### 2. CMake
```bash
# Via Homebrew
brew install cmake

# Or download from https://cmake.org/download/
```

### 3. LLVM

**Option A: Build from source (recommended for self-hosting)**
See `../chapter-03.md` for detailed instructions. This gives you full control.

**Option B: Use Homebrew LLVM**
```bash
brew install llvm
```

## Building with Homebrew LLVM

If using Homebrew LLVM, you need to tell CMake where to find it:

```bash
# Find Homebrew LLVM location
brew --prefix llvm

# Configure with explicit LLVM path
cmake -B build -DLLVM_CONFIG=$(brew --prefix llvm)/bin/llvm-config

# Build
cmake --build build

# Test
./build/test_bridge
```

## Building with Source-Built LLVM

If you built LLVM from source to `$HOME/llvm-21-with-clang-lld-lldb`:

```bash
# Configure (CMake will auto-detect this location)
cmake -B build

# Or specify explicitly
cmake -B build -DLLVM_PREFIX=$HOME/llvm-21-with-clang-lld-lldb

# Build
cmake --build build

# Test
./build/test_bridge
```

## Building with System Clang

The project will work with macOS system clang++ as long as LLVM libraries are available:

```bash
# System clang version
clang++ --version

# Configure and build
cmake -B build
cmake --build build
```

## Common Issues on Mac

### Issue 1: zstd library not found

**Symptoms:**
```
ld: library 'zstd' not found
```

**Solution:**
```bash
# Install zstd via Homebrew
brew install zstd

# Rebuild
cmake --build build --clean-first
```

The CMakeLists.txt will automatically detect Homebrew's zstd location.

### Issue 2: llvm-config not found

**Symptoms:**
```
CMake Error: llvm-config not found
```

**Solution A: Add to PATH**
```bash
# For Homebrew LLVM
export PATH="$(brew --prefix llvm)/bin:$PATH"

# For source-built LLVM
export PATH="$HOME/llvm-21-with-clang-lld-lldb/bin:$PATH"

# Then reconfigure
cmake -B build
```

**Solution B: Specify explicitly**
```bash
cmake -B build -DLLVM_CONFIG=/path/to/llvm-config
```

### Issue 3: C++ standard library issues

**Symptoms:**
```
ld: library not found for -lc++
```

**Solution:**
Make sure Xcode Command Line Tools are installed:
```bash
xcode-select --install
sudo xcode-select --switch /Library/Developer/CommandLineTools
```

## Architecture Notes

### Apple Silicon (M1/M2/M3)

The build system automatically detects Apple Silicon and uses the correct architecture.

```bash
# Verify architecture
file build/test_bridge
# Should show: arm64
```

### Intel Macs

Works the same way, just builds for x86_64:

```bash
file build/test_bridge
# Should show: x86_64
```

## Verifying the Build

After a successful build:

```bash
# 1. Test program exists
ls -lh build/test_bridge

# 2. Run the test
./build/test_bridge

# 3. Check generated object file
ls -lh test_add.o
file test_add.o

# 4. Inspect the object file
objdump -d test_add.o
```

Expected output from test_bridge:
```
=== LLVM Bridge Test ===
Creating LLVM context...
Creating LLVM module...
...
=== All tests passed! ===
```

## Clean Rebuild

If you need to start fresh:

```bash
# Remove build directory
rm -rf build test_add.o

# Reconfigure and build
cmake -B build
cmake --build build
```

## CMake Configuration Summary

The CMakeLists.txt handles:
- ✅ Auto-detection of Homebrew LLVM
- ✅ Auto-detection of Homebrew zstd
- ✅ System clang++ compatibility
- ✅ Apple Silicon and Intel Macs
- ✅ Proper rpath for dynamic libraries
- ✅ LLVM cxxflags and ldflags integration

## Next Steps

Once `./build/test_bridge` runs successfully:

1. ✅ Phase 0 is complete
2. 📝 Review `SELFHOSTING.md` for Phase 1 plan
3. 🚀 Start implementing `lexer.pyxc`

## Getting Help

If build issues persist:
1. Check `cmake -B build --debug-output` for detailed info
2. Verify LLVM installation: `llvm-config --version`
3. Check system compiler: `clang++ --version`
4. Review `../chapter-03.md` for LLVM build instructions
