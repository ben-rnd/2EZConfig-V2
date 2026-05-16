add_library(autoplay STATIC autoplay.cpp)
target_include_directories(autoplay
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(autoplay PRIVATE logger kernel32 user32)
