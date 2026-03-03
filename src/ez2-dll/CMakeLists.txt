# 2EZ.dll
add_library(2EZ SHARED dll.cpp)
set_target_properties(2EZ PROPERTIES PREFIX "" OUTPUT_NAME "2EZ")
target_link_libraries(2EZ PRIVATE 
    kernel32 
    user32
)
