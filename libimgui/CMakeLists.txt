file(GLOB IMGUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set(BACKEND backends/imgui_impl_opengl3.cpp backends/imgui_impl_sdl2.cpp)

add_library(imgui_static STATIC ${IMGUI_SOURCES} ${BACKEND})
target_include_directories(imgui_static PUBLIC ./include ./ ${SDL2_INCLUDE_DIRS})
target_link_libraries(imgui_static PRIVATE OpenGL::GL GLEW::GLEW ${SDL2_LIBRARIES})