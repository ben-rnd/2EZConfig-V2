# 2EZConfig.exe
add_executable(2EZConfig WIN32 main.cpp app.rc)
set_source_files_properties(app.rc PROPERTIES
    COMPILE_FLAGS "--include-dir=${CMAKE_SOURCE_DIR}/src"
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/resources/patches.json"
)

target_include_directories(2EZConfig PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(2EZConfig PRIVATE
    bindings
    injector
    patches
    imgui
    opengl32
    gdi32
    setupapi
    hid
)
