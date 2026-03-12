add_library(settings settings.cpp)

target_include_directories(settings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(settings
    PUBLIC
        nlohmann_json::nlohmann_json
        patches
        logger
)
