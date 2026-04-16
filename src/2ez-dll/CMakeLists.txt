# 2EZ.dll
add_library(2EZ SHARED
    dll.cpp
    ez2dj-io/ez2dj_io_hooks.cpp
    ez2dj-io/ez2dj_io_input.cpp
    ez2dj-io/ez2dj_io_output.cpp
    ez2dancer-io/ez2dancer_io_hooks.cpp
    ez2dancer-io/ez2dancer_io_input.cpp
    ez2dancer-io/ez2dancer_io_output.cpp
    sabin-io/sabin_io_hooks.cpp
    sabin-io/sabin_io_input.cpp
    sabin-io/sabin_io_output.cpp
    ddraw/ddraw4_fix.cpp
    ddraw/ddraw7_fix.cpp
    ddraw/ddraw_vertex_utils.cpp
    dsound-eq/dsound_eq.cpp
    dsound-eq/mixer_hook.cpp
    dsound-eq/biquad.c
    $<$<CONFIG:Debug>:dll_hardlock_debug.cpp>
)
set_target_properties(2EZ PROPERTIES PREFIX "" OUTPUT_NAME "2EZ")

target_include_directories(2EZ PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ez2dj-io
    ${CMAKE_CURRENT_SOURCE_DIR}/ez2dancer-io
    ${CMAKE_CURRENT_SOURCE_DIR}/sabin-io
    ${CMAKE_CURRENT_SOURCE_DIR}/ddraw
    ${CMAKE_CURRENT_SOURCE_DIR}/dsound-eq
)

target_link_libraries(2EZ PRIVATE
    hardlock
    hooks
    bindings
    patches
    logger
    utilities
    kernel32
    user32
    winmm
    dsound
    ole32
    shell32
)
