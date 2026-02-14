UNAME_S := $(shell uname -s)

LLVM_PREFIX ?= $(HOME)/llvm-21-with-clang-lld-lldb-mlir
LLVM_CONFIG = $(LLVM_PREFIX)/bin/llvm-config
CXX = $(LLVM_PREFIX)/bin/clang++
CC = $(LLVM_PREFIX)/bin/clang

TARGET := pyxc
SRC := pyxc.cpp
RUNTIME_SRC := runtime.c
RUNTIME_OBJ := runtime.o

LLVM_FLAGS := $(shell $(LLVM_CONFIG) --cxxflags --ldflags --system-libs --libs all)
LLD_FLAGS := -llldCommon -llldELF -llldMachO -llldCOFF

CXXFLAGS := -g -O3
CFLAGS :=
LDFLAGS :=

# Optional user override for extra linker search path.
EXTRA_LIBDIR ?=
# Optional override for zstd library directory.
ZSTD_LIBDIR ?=

# Auto-detect zstd via pkg-config first.
ifeq ($(strip $(ZSTD_LIBDIR)),)
ifneq ($(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libzstd && echo yes),)
ZSTD_LIBDIR := $(shell pkg-config --variable=libdir libzstd)
endif
endif

# Fallback auto-detect for Homebrew installs.
ifeq ($(strip $(ZSTD_LIBDIR)),)
ifneq ($(shell command -v brew >/dev/null 2>&1 && brew --prefix zstd >/dev/null 2>&1 && echo yes),)
ZSTD_LIBDIR := $(shell brew --prefix zstd)/lib
endif
endif

ifneq ($(strip $(EXTRA_LIBDIR)),)
LDFLAGS += -L$(EXTRA_LIBDIR) -Wl,-rpath,$(EXTRA_LIBDIR)
endif

ifneq ($(strip $(ZSTD_LIBDIR)),)
LDFLAGS += -L$(ZSTD_LIBDIR) -Wl,-rpath,$(ZSTD_LIBDIR)
endif

ifeq ($(UNAME_S),Darwin)
SDKROOT ?= $(shell xcrun --show-sdk-path)
CFLAGS += -isysroot $(SDKROOT)
CXXFLAGS += -isysroot $(SDKROOT) -stdlib=libc++
ifneq (,$(wildcard /Library/Developer/CommandLineTools/usr/include/c++/v1))
CXXFLAGS += -I/Library/Developer/CommandLineTools/usr/include/c++/v1
endif
endif

RUNTIME_TARGET :=
ifneq (,$(wildcard $(RUNTIME_SRC)))
RUNTIME_TARGET := $(RUNTIME_OBJ)
endif

.PHONY: all clean

all: $(TARGET) $(RUNTIME_TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< $(LLVM_FLAGS) $(LDFLAGS) $(LLD_FLAGS) -o $@

$(RUNTIME_OBJ): $(RUNTIME_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f $(TARGET) $(RUNTIME_OBJ) *.o *.tmp.o
	@rm -rf *.dSYM
	@for f in *.pyxc; do \
		[ -e "$$f" ] || continue; \
		base="$${f%.pyxc}"; \
		rm -f "$$base" "$$base.o" "$$base.tmp.o"; \
	done
	@find . -maxdepth 1 -type f -perm -111 ! -name "*.sh" -delete
