cmake_minimum_required(VERSION 3.22)
project(libpxl C)

set(CMAKE_C_STANDARD 99)

if(WIN32)
    set(LIBPXL_BACKEND libpxl_win32.c)
else()
    message(FATAL_ERROR "no libpxl backend for this platform yet")
endif()

add_library(libpxl STATIC libpxl.c ${LIBPXL_BACKEND})
target_include_directories(libpxl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# the library must stay C89-compatible for DOS toolchains; the demo may use C99
set_target_properties(libpxl PROPERTIES C_STANDARD 90)
if(WIN32)
    target_link_libraries(libpxl PRIVATE user32 gdi32)
endif()

add_executable(demo main.c)
target_link_libraries(demo PRIVATE libpxl)
if(WIN32)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
