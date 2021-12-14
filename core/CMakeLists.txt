aux_source_directory (${CMAKE_SOURCE_DIR}/core/ CORE_SOURCES)

add_library (${LIB_CORE_NAME} STATIC ${CORE_SOURCES})

target_link_libraries (${LIB_CORE_NAME}
    ssl
    crypto
    pthread
    ${GIO_LIBRARIES}
    ${GLIB_LIBRARIES}
)
