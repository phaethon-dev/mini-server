ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    EXE      = .exe
    LDLIBS   = -lws2_32
    MKDIR    = mkdir $(subst /,\,$@)
    CLEAN    = if exist build rmdir /S /Q build
else
    PLATFORM = linux
    EXE      =
    LDLIBS   =
    MKDIR    = mkdir -p $@
    CLEAN    = rm -rf build
endif

BUILD = build/$(PLATFORM)

# Compiler and flags, set once and used everywhere
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wshadow -g -MMD -MP

# Shared code: every reusable .cpp goes in this list, and only this list
LIB_OBJS = $(BUILD)/http.o $(BUILD)/socket.o
LIB      = $(BUILD)/libserver.a

# Programs: every *_test.cpp is found automatically and compiled into its own program; the linker will pull in only the pieces of the library that are needed
PROGRAMS = $(patsubst %.cpp,$(BUILD)/%$(EXE),$(wildcard *_test.cpp))
APPS     = $(BUILD)/server$(EXE)

all: $(PROGRAMS) $(APPS)

# Bundle the shared objects into one static library
$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

# Any program = its own .o + the library; the linker takes only the pieces it needs
$(PROGRAMS) $(APPS): $(BUILD)/%$(EXE): $(BUILD)/%.o $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

# Compile each .cpp into a .o; header dependencies come from the generated .d files
$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	$(MKDIR)

# Run every program, one after another
RUN_TARGETS = $(addprefix run-,$(PROGRAMS))

run: $(RUN_TARGETS)

$(RUN_TARGETS): run-%: %
	./$<

# Delete everything that was built
clean:
	$(CLEAN)

.PHONY: all run clean $(RUN_TARGETS)

# Keep the .o files: make would otherwise delete them as 'intermediate' files
.SECONDARY:

# Pull in the dependency files that g++ generated (if any exist yet)
-include $(wildcard $(BUILD)/*.d)
