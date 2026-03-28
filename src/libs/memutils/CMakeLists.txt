add_library(memutils STATIC
    memutils.c
    hotpatch.c
)
target_include_directories(memutils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Suppress warnings from third-party code
target_compile_options(memutils PRIVATE -w)
