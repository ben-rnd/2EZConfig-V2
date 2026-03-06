add_library(bindings bindings.cpp)

target_include_directories(bindings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common/include
)

target_link_libraries(bindings
    PUBLIC
        input
        settings
)
