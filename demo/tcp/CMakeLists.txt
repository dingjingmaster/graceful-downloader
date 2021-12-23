aux_source_directory(. tcp_example)

foreach(src ${tcp_example})
    get_filename_component(mainName ${src} NAME_WE)
    add_executable(${mainName} ${src})
    target_link_libraries(${mainName} ${LIB_CORE_NAME})
endforeach(src)
