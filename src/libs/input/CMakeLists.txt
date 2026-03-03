# usb-hid-usage: two-file C library (Apache-2.0) — usage page/ID to human-readable name
# usbhidusage_SOURCE_DIR is populated by FetchContent_MakeAvailable(usbhidusage) in root CMakeLists.txt
add_library(input STATIC
    input.cpp
    ${usbhidusage_SOURCE_DIR}/usb-hid-usage.c
)

target_include_directories(input
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${usbhidusage_SOURCE_DIR}
)

target_link_libraries(input
    PUBLIC
        settings
        hid
        setupapi
        winmm
)
