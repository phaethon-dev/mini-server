# Compiler and flags, set once and used everywhere
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wshadow -g -MMD -MP
LDLIBS   = -lws2_32

# Shared code: every reusable .cpp goes in this list, and only this list
LIB_OBJS = http.o socket.o
LIB      = libserver.a

# Programs: every *_test.cpp is found automatically and compiled into its own .exe; the linker will pull in only the pieces of the library that are needed
PROGRAMS = $(patsubst %.cpp,%.exe,$(wildcard *_test.cpp))

APPS = server.exe

all: $(PROGRAMS) $(APPS)

# Bundle the shared objects into one static library
$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

# Any program = its own .o + the library; the linker takes only the pieces it needs
%.exe: %.o $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

# Compile each .cpp into a .o; header dependencies come from the generated .d files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Run every program, one after another
RUN_TARGETS = $(addprefix run-,$(PROGRAMS))

run: $(RUN_TARGETS)

$(RUN_TARGETS): run-%: %
	./$<

# Delete everything that was built
clean:
	del /Q *.o *.d *.a *.exe

.PHONY: all run clean $(RUN_TARGETS)

# Keep the .o files: make would otherwise delete them as 'intermediate' files
.SECONDARY:

# Pull in the dependency files that g++ generated (if any exist yet)
-include $(wildcard *.d)
