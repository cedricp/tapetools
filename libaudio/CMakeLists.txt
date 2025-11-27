file(GLOB PAAUDIO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(paaudio_static STATIC ${PAAUDIO_SOURCES}) 
target_include_directories(paaudio_static PUBLIC include ${PROJECT_SOURCE_DIR}/libutils/include ${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(paaudio_static PRIVATE ${PORTAUDIO_LIBRARIES})
