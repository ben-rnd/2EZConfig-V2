add_library(hypersonik STATIC
    src/converter.c
    src/ds-api.c
    src/ds-buffer.c
    src/ds-buffer-pri.c
    src/hr.c
    src/list.c
    src/queue.c
    src/reaper.c
    src/refcount.c
    src/snd-buffer.c
    src/snd-mixer.c
    src/snd-service.c
    src/snd-stream.c
    src/trace.c
    src/wasapi.c
    guid/guid.c
)

target_include_directories(hypersonik
    PUBLIC src guid
    PRIVATE ${CMAKE_SOURCE_DIR}/src/2ez-dll/audio-eq
)

target_compile_options(hypersonik PRIVATE
    -DCOBJMACROS
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0600
    -UWINVER -DWINVER=0x0600
    -DHYPERSONIK_EMBEDDED
    -w
)

# Most sources use the precompiled header, but guid.c must NOT
# (it needs initguid.h to take effect before system headers)
file(GLOB _hypersonik_src_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
set_source_files_properties(${_hypersonik_src_files} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/precompiled.h"
)

target_link_libraries(hypersonik PRIVATE msacm32)
