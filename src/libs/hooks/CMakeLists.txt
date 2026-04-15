add_library(hooks STATIC
    hooks.cpp
)
target_include_directories(hooks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hooks PUBLIC minhook PRIVATE logger)
