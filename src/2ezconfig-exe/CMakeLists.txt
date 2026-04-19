# Embed THIRD_PARTY_LICENSES as a C string
file(READ "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES" _licenses_raw)
string(REPLACE "\\" "\\\\" _licenses_escaped "${_licenses_raw}")
string(REPLACE "\"" "\\\"" _licenses_escaped "${_licenses_escaped}")
string(REPLACE "\n" "\\n\"\n\"" _licenses_escaped "${_licenses_escaped}")
set(LICENSES_STRING "\"${_licenses_escaped}\"")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/licenses.h"
    @ONLY
)

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
    ${CMAKE_CURRENT_BINARY_DIR}
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
