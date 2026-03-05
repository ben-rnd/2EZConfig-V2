add_library(patches patch_store.cpp)
target_include_directories(patches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(patches PUBLIC nlohmann_json::nlohmann_json psapi)
