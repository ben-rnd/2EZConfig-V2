add_library(logger STATIC logger.cpp)
target_include_directories(logger 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libs/logger
)
