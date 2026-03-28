add_library(hardlock STATIC
    io.hardlock.hooks.c
    io.hardlock.emulator.c
    io.hardlock.internal.c
    io.hardlock.protocol.c
    ks.debug.c
    ks.misc.c
)
target_include_directories(hardlock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hardlock PUBLIC memutils)

# Suppress warnings from third-party code
target_compile_options(hardlock PRIVATE -w)
