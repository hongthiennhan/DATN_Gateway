cmake_minimum_required(VERSION 3.10)
project(DCS_Test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod)

message(STATUS "GPIOD include dirs: ${GPIOD_INCLUDE_DIRS}")
message(STATUS "GPIOD library dirs: ${GPIOD_LIBRARY_DIRS}")
message(STATUS "GPIOD libraries: ${GPIOD_LIBRARIES}")

pkg_check_modules(NCURSES REQUIRED ncurses)

message(STATUS "NCURSES include dirs: ${NCURSES_INCLUDE_DIRS}")
message(STATUS "NCURSES library dirs: ${NCURSES_LIBRARY_DIRS}")
message(STATUS "NCURSES libraries: ${NCURSES_LIBRARIES}")


# Include directories
set(INCLUDE_DIRS
    esp_command
    i2c_ssd1306
    Inc
)

# Source files thread 1
set(SOURCES_1
    Src_process/led_process.c)

# Add executable 1
add_executable(led_app ${SOURCES_1})

# Include directories
target_include_directories(led_app PRIVATE ${INCLUDE_DIRS})

# Link libgpiod
target_link_libraries(led_app ${GPIOD_LIBRARIES})

# Source files thread 2
set(SOURCES_2
    Src_process/main_process.c
    esp_command/control_command.c
    i2c_ssd1306/ssd1306.c
    i2c_ssd1306/linux_i2c.c
    i2c_ssd1306/font.c)

# Add executable 2
add_executable(main_app ${SOURCES_2})

# Include directories for main_app
target_include_directories(main_app PRIVATE ${INCLUDE_DIRS})

target_link_libraries(main_app ${GPIOD_LIBRARIES} ${NCURSES_LIBRARIES})