cmake_minimum_required(VERSION 3.25.0)
project(tapetools_project C CXX)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH ${tapetools_project_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set(CMAKE_INSTALL_PREFIX ${tapetools_project_SOURCE_DIR}/INSTALL_${CMAKE_BUILD_TYPE})

if(UNIX)
set(OpenGL_GL_PREFERENCE "GLVND")
endif(UNIX)

option(WITH_RTLSDR "Build with RTL-SDR support" OFF)

set(ARCH elf64-x86-64)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(SDL2 REQUIRED)
find_package(FFTW REQUIRED)
find_package(PortAudio REQUIRED)
if (WITH_RTLSDR)
    find_package(LIBUSB REQUIRED)
endif()

message(STATUS "FFTW3 libs ${FFTW_DOUBLE_LIB}")
message(STATUS "FFTW3 includes ${FFTW_INCLUDE_DIRS}")
message(STATUS "SLD2 libraries ${SDL2_LIBRARIES}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

add_subdirectory(libdsp)
add_subdirectory(libaudio)
add_subdirectory(libimgui)
add_subdirectory(libutils)
if (WITH_RTLSDR)
    add_subdirectory(libsdr)
endif()

set(CPP_FILES main.cpp audio_draw.cpp audio_compute.cpp main_widget.cpp wow_flutter_thread.cpp)

if (WITH_RTLSDR)
    set(RTL_LIBS rtlsdr_static)
    add_compile_definitions(RTL_SDR)
    list(APPEND CPP_FILES sdr_draw.cpp)
endif()
add_subdirectory(resources)

# Executable rules

add_executable(tapetools ${CPP_FILES})
target_include_directories(tapetools PRIVATE ${FFTW_INCLUDE_DIRS} "libimgui/include" "libpaaudio/include" "libutils/include" "resources" "libsdr/include")

# Link rules

if(MINGW)
set(PLATFORM_LIBS shlwapi winmm)
target_sources(tapetools PRIVATE resources/tapetools.rc)
endif()

if(MINGW)
    set(LINKFLAGS "-static-libgcc -static-libstdc++")
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(LINKFLAGS "${LINKFLAGS} -mwindows")   
    endif()
    add_compile_definitions("-DWIN32")
    set_target_properties(tapetools PROPERTIES LINK_FLAGS "${LINKFLAGS}")
else()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    endif()
endif(MINGW)

target_link_libraries(tapetools ${FFTW_DOUBLE_LIB} dsp_static paaudio_static imgui_static utils_static ${PORTAUDIO_LIBRARIES} ${RTL_LIBS} $<LINK_LIBRARY:WHOLE_ARCHIVE,resources_static> ${PLATFORM_LIBS} ${DBG_LIBS})


# Install rules

install (TARGETS tapetools RUNTIME DESTINATION bin)

if(MINGW)
    set(CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/install_deps_${CMAKE_BUILD_TYPE}.cmake)

    #
    # Find and install needed DLLs
    #
    file(GENERATE OUTPUT
    "${CONFIG_FILE}" CONTENT 
    [[
    set(TARGET_APP $<TARGET_FILE:tapetools>)
    message(STATUS "path to executable: ${TARGET_APP}")
    message(STATUS "paths to cmake library paths ${CMAKELIBPATH}")
    message(STATUS "Looking for deps in '${TARGET_APP}'")
    file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR deps_resolved
    UNRESOLVED_DEPENDENCIES_VAR deps_unresolved
    LIBRARIES ${TARGET_APP}
    DIRECTORIES ${CMAKELIBPATH}
    PRE_EXCLUDE_REGEXES "api-ms-*" "ext-ms-*"
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
    )
    message(STATUS "Resolving runtime dependencies for ${TARGET_APP}")
    foreach(dep ${deps_resolved})
    file(INSTALL ${dep} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
    message(STATUS "Installing ${dep}")
    endforeach()
    foreach(dep ${deps_unresolved})
    message(WARNING "Runtime dependency ${dep} could not be resolved.")
    endforeach()
    ]]
    )

    get_filename_component(MIGGWCC  ${CMAKE_C_COMPILER} PATH)
	set(MINGWLIBPATH "${MIGGWCC}/")
    install(CODE "set(CMAKELIBPATH \"${CMAKE_SYSTEM_LIBRARY_PATH};${CMAKE_MINGW_SYSTEM_LIBRARY_PATH};${MINGWLIBPATH}\")")
    install(SCRIPT ${CONFIG_FILE})
endif(MINGW)

