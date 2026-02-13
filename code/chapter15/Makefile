UNAME_S := $(shell uname -s)

LLVM_PREFIX ?= $(HOME)/llvm-21-with-clang-lld-lldb-mlir
LLVM_CONFIG = $(LLVM_PREFIX)/bin/llvm-config
CXX = $(LLVM_PREFIX)/bin/clang++
CC = $(LLVM_PREFIX)/bin/clang
HOMEBREW_LIB ?= /opt/homebrew/lib

TARGET := pyxc
SRC := pyxc.cpp
RUNTIME_SRC := runtime.c
RUNTIME_OBJ := runtime.o

LLVM_FLAGS := $(shell $(LLVM_CONFIG) --cxxflags --ldflags --system-libs --libs all)
LLD_FLAGS := -llldCommon -llldELF -llldMachO -llldCOFF

CXXFLAGS := -g -O3
CFLAGS :=
LDFLAGS :=

ifeq ($(UNAME_S),Darwin)
SDKROOT ?= $(shell xcrun --show-sdk-path)
CFLAGS += -isysroot $(SDKROOT)
CXXFLAGS += -isysroot $(SDKROOT) -stdlib=libc++
ifneq (,$(wildcard /Library/Developer/CommandLineTools/usr/include/c++/v1))
CXXFLAGS += -I/Library/Developer/CommandLineTools/usr/include/c++/v1
endif
ifneq ($(strip $(HOMEBREW_LIB)),)
LDFLAGS += -L$(HOMEBREW_LIB) -Wl,-rpath,$(HOMEBREW_LIB)
endif
endif

ifeq ($(UNAME_S),Linux)
ifneq ($(strip $(HOMEBREW_LIB)),)
LDFLAGS += -L$(HOMEBREW_LIB) -Wl,-rpath,$(HOMEBREW_LIB)
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
	rm -f $(TARGET) $(RUNTIME_OBJ)
