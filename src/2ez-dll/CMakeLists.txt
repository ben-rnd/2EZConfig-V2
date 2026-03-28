# 2EZ.dll
add_library(2EZ SHARED
    dll.cpp
    ez2-io/ez2_io_hooks.cpp
    ez2-io/ez2_io_input.cpp
    ez2-io/ez2_io_output.cpp
    sabin-io/sabin_io_hooks.cpp
    sabin-io/sabin_io_input.cpp
    sabin-io/sabin_io_output.cpp
    $<$<CONFIG:Debug>:dll_hardlock_debug.cpp>
)
set_target_properties(2EZ PROPERTIES PREFIX "" OUTPUT_NAME "2EZ")

target_include_directories(2EZ PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ez2-io
    ${CMAKE_CURRENT_SOURCE_DIR}/sabin-io
)

target_link_libraries(2EZ PRIVATE
    hardlock
    memutils
    bindings
    patches
    logger
    utilities
    kernel32
    user32
    winmm
)
