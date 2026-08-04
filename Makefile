# ---------------------------------------------------------------------------
# c-test — build & run
#
#   make run gcc        build with GCC  and run all tests
#   make run clang      build with Clang and run all tests
#   make run msvc       build with MSVC (Wine) and run all tests
#
#   make run++ gcc      build C++ tests with g++ and run
#   make run++ clang    build C++ tests with clang++ and run
#   make run++ msvc     build C++ tests with MSVC cl (C++20) and run
#
#   make clean          remove dist/
# ---------------------------------------------------------------------------

# ---------- compiler detection / subcommand trick --------------------------
# Allows:  make run gcc  (treats second word as COMPILER)
ifeq (run,$(firstword $(MAKECMDGOALS)))
  COMPILER := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(COMPILER):;@:)
endif
ifeq (run++,$(firstword $(MAKECMDGOALS)))
  COMPILER := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(COMPILER):;@:)
endif
COMPILER ?= gcc

# ---------- paths -----------------------------------------------------------
DIST     := dist
TEST_DIR := test

# Test binaries built from test/*.c and test/*.cpp (each file → one binary)
TEST_SRCS     := $(wildcard $(TEST_DIR)/*.c)
TEST_CXX_SRCS := $(wildcard $(TEST_DIR)/*.cpp)

# ---------- GCC / Clang C flags ---------------------------------------------
STD      ?= gnu99
WARN     := -Wall -Wextra -Wno-missing-field-initializers
DBGFLAG  := -g

# ---------- GCC / Clang C++ flags -------------------------------------------
CXXSTD_GCC   ?= c++11
CXXSTD_CLANG ?= c++11
CXXWARN      := -Wall -Wextra -Wno-missing-field-initializers

# ---------- MSVC flags -------------------------------------------------------
MSVC_BIN   ?= /root/.msvc/bin/x64
CL         := $(MSVC_BIN)/cl
MSVC_FLAGS := /W3 /nologo /D_CRT_SECURE_NO_WARNINGS
MSVC_CXX_STD := /std:c++20

# ---------------------------------------------------------------------------

.PHONY: all run run++ clean strict

# Default: build with gcc
all: run

# --- run target -------------------------------------------------------------
run: _build_$(COMPILER)
	@echo ""
	@$(MAKE) --no-print-directory _exec_$(COMPILER)

# --- run++ target -----------------------------------------------------------
run++: _build_cxx_$(COMPILER)
	@echo ""
	@$(MAKE) --no-print-directory _exec_cxx_$(COMPILER)

# --- GCC -------------------------------------------------------------------
_build_gcc: _ensure_dist
	gcc -std=$(STD) $(WARN) $(DBGFLAG) -o $(DIST)/c-test ctest.c
	@for src in $(TEST_SRCS); do \
	    name=$$(basename $$src .c); \
	    echo "  cc $$name"; \
	    gcc -std=$(STD) $(WARN) $(DBGFLAG) -I. -DCTEST -o $(DIST)/$$name $$src -ldl; \
	done

_exec_gcc:
	$(DIST)/c-test $(patsubst $(TEST_DIR)/%.c,$(DIST)/%,$(TEST_SRCS))

_build_cxx_gcc: _ensure_dist
	gcc -std=$(STD) $(WARN) $(DBGFLAG) -o $(DIST)/c-test ctest.c
	@for src in $(TEST_CXX_SRCS); do \
	    name=$$(basename $$src .cpp); \
	    echo "  g++ $$name"; \
	    g++ -std=$(CXXSTD_GCC) $(CXXWARN) $(DBGFLAG) -I. -DCTEST -o $(DIST)/$$name $$src; \
	done

_exec_cxx_gcc:
	$(DIST)/c-test $(patsubst $(TEST_DIR)/%.cpp,$(DIST)/%,$(TEST_CXX_SRCS))

# --- Clang -----------------------------------------------------------------
_build_clang: _ensure_dist
	clang -std=$(STD) $(WARN) $(DBGFLAG) -o $(DIST)/c-test ctest.c
	@for src in $(TEST_SRCS); do \
	    name=$$(basename $$src .c); \
	    echo "  cc $$name"; \
	    clang -std=$(STD) $(WARN) $(DBGFLAG) -I. -DCTEST -o $(DIST)/$$name $$src -ldl; \
	done

_exec_clang:
	$(DIST)/c-test $(patsubst $(TEST_DIR)/%.c,$(DIST)/%,$(TEST_SRCS))

_build_cxx_clang: _ensure_dist
	clang -std=$(STD) $(WARN) $(DBGFLAG) -o $(DIST)/c-test ctest.c
	@for src in $(TEST_CXX_SRCS); do \
	    name=$$(basename $$src .cpp); \
	    echo "  clang++ $$name"; \
	    clang++ -std=$(CXXSTD_CLANG) $(CXXWARN) $(DBGFLAG) -I. -DCTEST -o $(DIST)/$$name $$src; \
	done

_exec_cxx_clang:
	$(DIST)/c-test $(patsubst $(TEST_DIR)/%.cpp,$(DIST)/%,$(TEST_CXX_SRCS))

# --- MSVC ------------------------------------------------------------------
TEST_EXES := $(patsubst $(TEST_DIR)/%.c,$(DIST)/%.exe,$(TEST_SRCS))

_build_msvc: _ensure_dist
	wineserver -k || true
	$(CL) $(MSVC_FLAGS) ctest.c /Fo:$(DIST)/ /Fe:$(DIST)/c-test.exe
	@for src in $(TEST_SRCS); do \
	    name=$$(basename $$src .c); \
	    echo "  cl $$name"; \
	    $(CL) $(MSVC_FLAGS) /I. /DCTEST $$src /Fo:$(DIST)/ /Fe:$(DIST)/$$name.exe; \
	done

_exec_msvc:
	WINEDLLOVERRIDES="winedbg.exe=" WINEDEBUG=fixme-all wine $(DIST)/c-test.exe $(patsubst $(TEST_DIR)/%.c,$(DIST)/%.exe,$(TEST_SRCS))

_build_cxx_msvc: _ensure_dist
	wineserver -k || true
	$(CL) $(MSVC_FLAGS) ctest.c /Fo:$(DIST)/ /Fe:$(DIST)/c-test.exe
	@for src in $(TEST_CXX_SRCS); do \
	    name=$$(basename $$src .cpp); \
	    echo "  cl++ $$name"; \
	    $(CL) $(MSVC_FLAGS) $(MSVC_CXX_STD) /EHsc /I. /DCTEST $$src /Fo:$(DIST)/ /Fe:$(DIST)/$$name.exe; \
	done

_exec_cxx_msvc:
	WINEDLLOVERRIDES="winedbg.exe=" WINEDEBUG=fixme-all wine $(DIST)/c-test.exe $(patsubst $(TEST_DIR)/%.cpp,$(DIST)/%.exe,$(TEST_CXX_SRCS))

# --- helpers ----------------------------------------------------------------
_ensure_dist:
	@mkdir -p $(DIST)

clean:
	rm -rf $(DIST)

# Rebuild with warnings-as-errors
strict:
	$(MAKE) run COMPILER=$(COMPILER) WARN="-Wall -Wextra -Werror -Wno-missing-field-initializers"
