add_library(input STATIC
    input_manager.cpp
)

target_include_directories(input
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(input
    PUBLIC
        hid
        setupapi
        winmm
        utilities
)
