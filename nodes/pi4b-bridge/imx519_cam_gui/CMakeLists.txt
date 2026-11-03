cmake_minimum_required(VERSION 3.16)
project(imx519_cam_gui CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    -O3
    -march=armv8-a+crc+simd
    -mtune=cortex-a72
    -ffast-math
    -flto
    -Wall
    -Wextra
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${LIBCAMERA_INCLUDE_DIRS}
)

set(SOURCES
    src/main.cpp
    src/camera_manager.cpp
)

add_executable(imx519_cam_gui ${SOURCES})

target_link_libraries(
    imx519_cam_gui
    PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${LIBCAMERA_LIBRARIES}
    pthread
)