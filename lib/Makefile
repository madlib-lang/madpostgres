# Inputs
CXX ?= clang++
TARGET ?= MACOS_ARM64
AR ?= llvm-ar


INCLUDEDIR := include
BUILDDIR := build
SRCDIR := src
OBJS :=\
  $(BUILDDIR)/madpostgres.o\
  $(BUILDDIR)/pquvutils.o\
  $(BUILDDIR)/pquv.o\

MADLIB_RUNTIME_HEADERS_PATH := $(shell madlib config runtime-headers-path)
MADLIB_RUNTIME_LIB_HEADERS_PATH := $(shell madlib config runtime-lib-headers-path)

build: prepare build/libmadpostgres.a print_done

prepare:
	@mkdir -p build

print_done:
	@echo "build done.\noutput: build/libmadpostgres.a"

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) -g -I$(INCLUDEDIR) -I$(MADLIB_RUNTIME_HEADERS_PATH) -I$(MADLIB_RUNTIME_LIB_HEADERS_PATH) -std=c++2a -O2 -fPIC $(CXXFLAGS) -c $< -o $@

build/libmadpostgres.a: $(OBJS)
	$(AR) rc $@ $^
