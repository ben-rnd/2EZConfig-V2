# EZ2Config.exe
add_executable(EZ2Config WIN32 main.cpp)

target_link_libraries(EZ2Config PRIVATE
    injector
    imgui
    input
    settings
    opengl32
    gdi32
    dwmapi
    setupapi
    hid
    version
)
