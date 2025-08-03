cmake_minimum_required(VERSION 3.10)
project(DCS_Test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncurses)

message(STATUS "NCURSES include dirs: ${NCURSES_INCLUDE_DIRS}")
message(STATUS "NCURSES library dirs: ${NCURSES_LIBRARY_DIRS}")
message(STATUS "NCURSES libraries: ${NCURSES_LIBRARIES}")

# Find mosquitto using pkg-config
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
message(STATUS "MOSQUITTO include dirs: ${MOSQUITTO_INCLUDE_DIRS}")
message(STATUS "MOSQUITTO library dirs: ${MOSQUITTO_LIBRARY_DIRS}")
message(STATUS "MOSQUITTO libraries: ${MOSQUITTO_LIBRARIES}")

# Include directories
set(INCLUDE_DIRS
    esp_command
    Inc
)

# Source files
set(SOURCES_2
    Src_process/main_process.c
    Src_process/thread_func.c
    Src_process/mqtt_thread.c
    esp_command/control_command.c)

# Add executable 2
add_executable(main_app ${SOURCES_2})

# Include directories for main_app
target_include_directories(main_app PRIVATE ${INCLUDE_DIRS})

target_link_libraries(main_app ${NCURSES_LIBRARIES} ${MOSQUITTO_LIBRARIES})