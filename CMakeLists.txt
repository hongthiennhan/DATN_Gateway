cmake_minimum_required(VERSION 3.10)
project(DCS_Test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# Find required libraries
pkg_check_modules(NCURSES REQUIRED ncurses)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

# NEW: Find json-c library
pkg_check_modules(JSON_C REQUIRED json-c)

message(STATUS "NCURSES include dirs: ${NCURSES_INCLUDE_DIRS}")
message(STATUS "NCURSES library dirs: ${NCURSES_LIBRARY_DIRS}")
message(STATUS "NCURSES libraries: ${NCURSES_LIBRARIES}")

message(STATUS "MOSQUITTO include dirs: ${MOSQUITTO_INCLUDE_DIRS}")
message(STATUS "MOSQUITTO library dirs: ${MOSQUITTO_LIBRARY_DIRS}")
message(STATUS "MOSQUITTO libraries: ${MOSQUITTO_LIBRARIES}")

# NEW: Debug output for json-c
message(STATUS "JSON_C include dirs: ${JSON_C_INCLUDE_DIRS}")
message(STATUS "JSON_C library dirs: ${JSON_C_LIBRARY_DIRS}")
message(STATUS "JSON_C libraries: ${JSON_C_LIBRARIES}")

# Include directories
set(INCLUDE_DIRS
    uart_handler
    config
    Inc
    mongoose_lib
    mqtt_thread
    tcp_thread
    udp_thread
)

# Source files for main application
set(SOURCES_2
    Src_process/main_process.c
    Src_process/thread_func.c
    mqtt_thread/mqtt_thread_new.c
    tcp_thread/tcp_thread.c
    udp_thread/udp_thread.c
    config/gateway_config.c
    uart_handler/uart_handler.c
    mongoose_lib/mongoose.c
)

set (TARGET main_app)
# Add main executable
add_executable(${TARGET} ${SOURCES_2})

# Include directories for main_app (UPDATED: Added JSON_C include dirs)
target_include_directories(${TARGET} PRIVATE 
    ${INCLUDE_DIRS}
    ${GPIOD_INCLUDE_DIRS}
    ${NCURSES_INCLUDE_DIRS}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
)

# Link libraries (UPDATED: Added JSON_C libraries)
target_link_libraries(${TARGET} 
    ${GPIOD_LIBRARIES} 
    ${NCURSES_LIBRARIES} 
    ${MOSQUITTO_LIBRARIES}
    ${JSON_C_LIBRARIES}
)

# Add compliler defiinitions
target_compile_definitions(${TARGET} PRIVATE 
    -DDEBUG
)

# NEW: Add compiler flags for all dependencies
target_compile_options(${TARGET} PRIVATE 
    ${GPIOD_CFLAGS_OTHER}
    ${NCURSES_CFLAGS_OTHER}
    ${MOSQUITTO_CFLAGS_OTHER}
    ${JSON_C_CFLAGS_OTHER}
)

# NEW: Copy config file to build directory
configure_file(${CMAKE_SOURCE_DIR}/nodes_config.json ${CMAKE_BINARY_DIR}/nodes_config.json COPYONLY)

# NEW: Install targets
install(TARGETS ${TARGET} DESTINATION bin)
install(FILES nodes_config.json DESTINATION bin)
