cmake_minimum_required(VERSION 3.22)
project(libpxl C)

set(CMAKE_C_STANDARD 99)

if(LIBPXL_DOS OR CMAKE_SYSTEM_NAME STREQUAL "DOS")
    set(LIBPXL_BACKEND libpxl_dos.c)
elseif(WIN32)
    set(LIBPXL_BACKEND libpxl_win32.c)
else()
    message(FATAL_ERROR "no libpxl backend for this platform yet")
endif()

set(LIBPXL_SOURCES
    libpxl.h
    libpxl_backend.h
    libpxl.c
    ${LIBPXL_BACKEND})

add_library(libpxl STATIC ${LIBPXL_SOURCES})
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

option(LIBPXL_BUILD_TESTS "build the libpxl tests" ON)
if(LIBPXL_BUILD_TESTS)
    enable_testing()
    add_executable(libpxl_tests tests/test_libpxl.c libpxl.c libpxl_null.c)
    target_include_directories(libpxl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(libpxl_tests PROPERTIES C_STANDARD 90)
    add_test(NAME libpxl_tests COMMAND libpxl_tests)
endif()

option(LIBPXL_BUILD_SINGLE "build the single-header distribution" ON)
if(LIBPXL_BUILD_SINGLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libpxl_single.h
        COMMAND ${CMAKE_COMMAND}
            -DLIBPXL_OUT=${CMAKE_CURRENT_BINARY_DIR}/libpxl_single.h
            "-DLIBPXL_FILES=${CMAKE_CURRENT_SOURCE_DIR}/libpxl.h;${CMAKE_CURRENT_SOURCE_DIR}/libpxl_backend.h;${CMAKE_CURRENT_SOURCE_DIR}/libpxl.c;${CMAKE_CURRENT_SOURCE_DIR}/${LIBPXL_BACKEND}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/amalgamate.cmake
        DEPENDS ${LIBPXL_SOURCES} cmake/amalgamate.cmake
        VERBATIM)
    add_custom_target(libpxl_single ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libpxl_single.h)

    add_executable(libpxl_single_check tests/single_include.c)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/libpxl_single.h PROPERTIES HEADER_FILE_ONLY TRUE)
    target_include_directories(libpxl_single_check PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    add_dependencies(libpxl_single_check libpxl_single)
    if(WIN32)
        target_link_libraries(libpxl_single_check PRIVATE user32 gdi32)
    endif()
endif()
