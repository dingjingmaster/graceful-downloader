project (subprojects)
cmake_minimum_required (VERSION 3.5)

set (CMAKE_C_STANDARD 99)
set (CMAKE_INCLUDE_CURRENT_DIR ON)
set (CMAKE_INCLUDE_CURRENT_DIR YES)
set (CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)

set (CMAKE_PROJECT_VERSION_MAJOR 1)
set (CMAKE_PROJECT_VERSION_MINOR 0)
set (CMAKE_PROJECT_VERSION_PATCH 0)
set (CMAKE_PROJECT_VERSION "${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}")

set (LIB_CORE_NAME "libgd-core")
set (LIB_CORE_NAME "libgd-core" CACHE STRING "libgd-core")

add_definitions (
    -D VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
    -D VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
    -D VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH}
)

find_package (PkgConfig)

pkg_check_modules (GLIB REQUIRED glib-2.0)
message ("PKG-CONFIG INCLUDE => " ${GLIB_INCLUDE_DIRS})
message ("PKG-CONFIG LINKED  => " ${GLIB_LIBRARIES})

pkg_check_modules (GIO REQUIRED gio-2.0)
message ("PKG-CONFIG INCLUDE => " ${GIO_INCLUDE_DIRS})
message ("PKG-CONFIG LINKED  => " ${GIO_LIBRARIES})

include_directories (
    ${CMAKE_SOURCE_DIR}/core
    ${GIO_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)


add_subdirectory (core)
add_subdirectory (test)
add_subdirectory (src)


option(BUILD_DOCUMENTATION "Use Doxygen to create the HTML based API documentation" OFF)
if(BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
    add_custom_target(doc ALL
            ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Generating API documentation with Doxygen" VERBATIM
            )
    install(DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/doc"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}"
            COMPONENT Devel
            )
endif()
