aux_source_directory(. http_example)

foreach(src ${http_example})
    get_filename_component (mainName ${src} NAME_WE)
    include_directories (${mainName} ${CMAKE_SOURCE_DIR})
    add_executable (${mainName} ${src})
    target_link_libraries (${mainName} ${LIB_CORE_NAME})
endforeach(src)
