# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/wurusai/irc/chirc

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/wurusai/irc/chirc/build

# Utility rule file for categories-assignment-4.

# Include any custom commands dependencies for this target.
include CMakeFiles/categories-assignment-4.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/categories-assignment-4.dir/progress.make

CMakeFiles/categories-assignment-4:
	../tests/print-categories.py ../tests/rubrics/assignment-4.json

categories-assignment-4: CMakeFiles/categories-assignment-4
categories-assignment-4: CMakeFiles/categories-assignment-4.dir/build.make
.PHONY : categories-assignment-4

# Rule to build all files generated by this target.
CMakeFiles/categories-assignment-4.dir/build: categories-assignment-4
.PHONY : CMakeFiles/categories-assignment-4.dir/build

CMakeFiles/categories-assignment-4.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/categories-assignment-4.dir/cmake_clean.cmake
.PHONY : CMakeFiles/categories-assignment-4.dir/clean

CMakeFiles/categories-assignment-4.dir/depend:
	cd /home/wurusai/irc/chirc/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/wurusai/irc/chirc /home/wurusai/irc/chirc /home/wurusai/irc/chirc/build /home/wurusai/irc/chirc/build /home/wurusai/irc/chirc/build/CMakeFiles/categories-assignment-4.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/categories-assignment-4.dir/depend

