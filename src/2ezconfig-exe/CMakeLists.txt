# 2EZConfig.exe
add_executable(2EZConfig WIN32 main.cpp app.rc)

execute_process(
    COMMAND powershell -Command "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('${CMAKE_SOURCE_DIR}/src').ShortPath"
    OUTPUT_VARIABLE _rc_short_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set_source_files_properties(app.rc PROPERTIES
    COMPILE_FLAGS "--include-dir ${_rc_short_path}"
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
