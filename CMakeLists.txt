cmake_minimum_required(VERSION 3.10)
project(maxcompress CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Find GTK3
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Find threads
find_package(Threads REQUIRED)

# Source files
set(SOURCES src/main.cpp)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/algorithms
    ${GTK3_INCLUDE_DIRS}
)

# Link directories
link_directories(${GTK3_LIBRARY_DIRS})

# Compiler flags
add_definitions(${GTK3_CFLAGS_OTHER})

# Build executable
add_executable(maxcompress ${SOURCES})
target_link_libraries(maxcompress
    ${GTK3_LIBRARIES}
    Threads::Threads
    z
)

# Install
install(TARGETS maxcompress DESTINATION bin)
