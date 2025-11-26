file(GLOB UTILS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(utils_static STATIC ${UTILS_SOURCES})
target_include_directories(utils_static PUBLIC ../libimgui/include include)
