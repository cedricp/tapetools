file(GLOB LIBDSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(dsp_static STATIC ${LIBDSP_SOURCES})
target_include_directories(dsp_static PUBLIC include)
