cmake_minimum_required (VERSION 3.5)

set (VERSION_MAJOR 1)
set (VERSION_MINOR 0)
set (VERSION_PATCH 0)

set (PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

project (graceful-downloader VERSION "${PROJECT_VERSION}" LANGUAGES C)

set (CMAKE_C_STANDARD 99)
set (CMAKE_INCLUDE_CURRENT_DIR ON)

#add_compile_options ()
#add_compile_definitions ("-DVERSION=${PROJECT_VERSION}")
add_definitions (
    -D VERSION_MAJOR=${VERSION_MAJOR}
    -D VERSION_MINOR=${VERSION_MINOR}
    -D VERSION_PATCH=${VERSION_PATCH}
)

find_package (PkgConfig)

pkg_search_module (GLIB REQUIRED glib-2.0)
message ("PKG-CONFIG INCLUDE => " ${GLIB_INCLUDE_DIRS})
message ("PKG-CONFIG LINKED  => " ${GLIB_LIBRARIES})

pkg_search_module (GIO REQUIRED gio-2.0)
message ("PKG-CONFIG INCLUDE => " ${GIO_INCLUDE_DIRS})
message ("PKG-CONFIG LINKED  => " ${GIO_LIBRARIES})

include (core/CMakeLists.txt)

add_subdirectory (test)


include_directories (
    ${CMAKE_SOURCE_DIR}/core
    ${GIO_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

add_executable (graceful-downloader
    ${PROJECT_SOURCES}
)

target_link_libraries (graceful-downloader
    ssl
    crypto
    pthread
    ${GIO_LIBRARIES}
    ${GLIB_LIBRARIES}
)
