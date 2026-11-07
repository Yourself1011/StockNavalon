# Thanks to Job Vranish (https://spin.atomicobject.com/2016/08/26/makefile-c-projects/)
TARGET_EXEC := final_program

# `make` builds release, `make dev` builds with debug info and sanitizers.
# Each mode gets its own build directory so their object files never mix.
MODE ?= release
BUILD_DIR := ./build/$(MODE)
SRC_DIRS := ./src

# Find all the C and C++ files we want to compile
# Note the single quotes around the * expressions. The shell will incorrectly expand these otherwise, but we want to send the * directly to the find command.
SRCS := $(shell find $(SRC_DIRS) -name '*.cc')

# Prepends BUILD_DIR and appends .o to every src file
# As an example, ./your_dir/hello.cpp turns into ./build/./your_dir/hello.cpp.o
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

# String substitution (suffix version without %).
# As an example, ./build/hello.cpp.o turns into ./build/hello.cpp.d
DEPS = $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

# Tests link every source except main.cc with the files in ./tests
TEST_EXEC := run_tests
TEST_SRCS := $(filter-out %/main.cc,$(SRCS)) $(shell find ./tests -name '*.cc')
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)

# Headers are included relative to src/game, e.g. "data/unitData.h"
INC_FLAGS := -I./src/game -I./third_party

# The -MMD and -MP flags together generate Makefiles for us!
# These files will have .d instead of .o as the output.
CPPFLAGS := $(INC_FLAGS) -MMD -MP
CXXFLAGS := -std=c++20 -Wall -Wextra

ifeq ($(MODE),dev)
# _GLIBCXX_DEBUG checks the standard library under libstdc++ (gcc),
# _LIBCPP_HARDENING_MODE does the same under libc++ (clang, macOS)
CXXFLAGS += -g -fsanitize=address,undefined -fno-omit-frame-pointer \
            -D_GLIBCXX_DEBUG \
            -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG
LDFLAGS += -fsanitize=address,undefined
else
CXXFLAGS += -O2 -DNDEBUG
endif

.PHONY: all
all: $(BUILD_DIR)/$(TARGET_EXEC)

.PHONY: dev
dev:
	$(MAKE) MODE=dev

# `make test` builds and runs the tests, in either mode
.PHONY: test
test: $(BUILD_DIR)/$(TEST_EXEC)
	$(BUILD_DIR)/$(TEST_EXEC)

# The final build step.
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(TEST_EXEC): $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $@ $(LDFLAGS)

# Build step for C++ source
$(BUILD_DIR)/%.cc.o: %.cc
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@


.PHONY: clean
clean:
	rm -rf ./build

# For clangd and other tools: every source and test file with the flags the
# build uses. Regenerated whenever the Makefile or the file list changes.
.PHONY: compile_commands
compile_commands: compile_commands.json

ALL_SRCS := $(sort $(SRCS) $(TEST_SRCS))
# The Python bindings need nanobind's and Python's headers, found through the
# project venv when there is one (see README)
VENV_PYTHON := .venv/bin/python
BINDINGS_FLAGS := $(shell [ -x $(VENV_PYTHON) ] && $(VENV_PYTHON) -c "import nanobind, sysconfig; print('-I' + nanobind.include_dir(), '-I' + sysconfig.get_paths()['include'])" 2>/dev/null)
compile_commands.json: Makefile $(ALL_SRCS) python/bindings.cc
	@printf '[\n' > $@
	@first=1; for f in $(ALL_SRCS); do \
		[ $$first = 1 ] || printf ',\n' >> $@; first=0; \
		printf '  {"directory": "%s", "file": "%s", "command": "$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c %s"}' \
			"$(CURDIR)" "$$f" "$$f" >> $@; \
	done
	@if [ -n "$(BINDINGS_FLAGS)" ]; then \
		printf ',\n  {"directory": "%s", "file": "python/bindings.cc", "command": "$(CXX) $(CPPFLAGS) $(BINDINGS_FLAGS) $(CXXFLAGS) -c python/bindings.cc"}' \
			"$(CURDIR)" >> $@; \
	fi
	@printf '\n]\n' >> $@
	@echo "wrote $@"

# Include the .d makefiles. The - at the front suppresses the errors of missing
# Makefiles. Initially, all the .d files will be missing, and we don't want those
# errors to show up.
-include $(DEPS)
