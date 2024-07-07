file(GLOB AUDIO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(audio_static STATIC ${AUDIO_SOURCES})
target_include_directories(audio_static PUBLIC ../libsoundio/include ../libimgui/include include)
add_compile_definitions(audio_static "SOUNDIO_STATIC_LIBRARY")
target_link_libraries(audio_static PRIVATE soundio_static)