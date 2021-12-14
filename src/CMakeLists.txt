project (graceful-downloader VERSION "${CMAKE_PROJECT_VERSION}" LANGUAGES C)

add_executable (graceful-downloader main.c)

target_link_libraries (graceful-downloader ${LIB_CORE_NAME})
set_target_properties (graceful-downloader PROPERTIES OUTPUT_NAME "${CMAKE_BINARY_DIR}/graceful-downloader")
