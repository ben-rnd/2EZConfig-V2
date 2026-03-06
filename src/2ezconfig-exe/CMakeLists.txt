# 2EZConfig.exe
add_executable(2EZConfig WIN32 main.cpp app.rc)

target_include_directories(2EZConfig PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(2EZConfig PRIVATE
    bindings
    injector
    patches
    imgui
    opengl32
    gdi32
    setupapi
    hid
)
