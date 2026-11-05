# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(
    SRCS ${app_sources}
    EMBED_FILES
        voice/ac_off.wav
        voice/ac_on_16.wav
        voice/set_19.wav
        voice/set_20.wav
        voice/set_21.wav
)
