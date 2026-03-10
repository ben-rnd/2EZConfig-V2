# 2EZ.dll
add_library(2EZ SHARED
    dll.cpp
    dll_input.cpp
    dll_output.cpp
)
set_target_properties(2EZ PROPERTIES PREFIX "" OUTPUT_NAME "2EZ")

target_include_directories(2EZ PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
)

target_link_libraries(2EZ PRIVATE
    hardlock
    bindings
    patches
    kernel32
    user32
    winmm
    psapi
)
