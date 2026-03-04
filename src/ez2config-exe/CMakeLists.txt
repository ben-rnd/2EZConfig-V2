# EZ2Config.exe
add_executable(EZ2Config WIN32 main.cpp bindings.cpp app.rc)

target_include_directories(EZ2Config PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(EZ2Config PRIVATE
    injector
    imgui
    input
    settings
    opengl32
    gdi32
    setupapi
    hid
)
