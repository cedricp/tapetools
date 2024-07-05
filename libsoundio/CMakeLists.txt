
option(ENABLE_JACK "Enable JACK backend" ON)
option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_ALSA "Enable ALSA backend" ON)
option(ENABLE_COREAUDIO "Enable CoreAudio backend" ON)
option(ENABLE_WASAPI "Enable WASAPI backend" ON)

set(LIBSOUNDIO_VERSION_MAJOR 2)
set(LIBSOUNDIO_VERSION_MINOR 0)
set(LIBSOUNDIO_VERSION_PATCH 0)

if(ENABLE_JACK)
    find_package(JACK)
    if(JACK_FOUND)
        set(STATUS_JACK "OK")
        set(SOUNDIO_HAVE_JACK true)
        include_directories(${JACK_INCLUDE_DIR})
    else()
        set(STATUS_JACK "not found")
        set(SOUNDIO_HAVE_JACK false)
        set(JACK_LIBRARY "")
    endif()
else()
    set(STATUS_JACK "disabled")
    set(SOUNDIO_HAVE_JACK false)
    set(JACK_LIBRARY "")
endif()

if(ENABLE_PULSEAUDIO)
    find_package(PulseAudio)
    if(PULSEAUDIO_FOUND)
        set(STATUS_PULSEAUDIO "OK")
        set(SOUNDIO_HAVE_PULSEAUDIO true)
        include_directories(${PULSEAUDIO_INCLUDE_DIR})
    else()
        set(STATUS_PULSEAUDIO "not found")
        set(SOUNDIO_HAVE_PULSEAUDIO false)
        set(PULSEAUDIO_LIBRARY "")
    endif()
else()
    set(STATUS_PULSEAUDIO "disabled")
    set(SOUNDIO_HAVE_PULSEAUDIO false)
    set(PULSEAUDIO_LIBRARY "")
endif()

if(ENABLE_ALSA)
    find_package(ALSA)
    if(ALSA_FOUND)
        set(STATUS_ALSA "OK")
        set(SOUNDIO_HAVE_ALSA true)
        include_directories(${ALSA_INCLUDE_DIRS})
    else()
        set(STATUS_ALSA "not found")
        set(SOUNDIO_HAVE_ALSA false)
        set(ALSA_LIBRARIES "")
    endif()
else()
    set(STATUS_ALSA "disabled")
    set(SOUNDIO_HAVE_ALSA false)
    set(ALSA_LIBRARIES "")
endif()

if(ENABLE_COREAUDIO)
    find_package(CoreAudio)
    if(COREAUDIO_FOUND)
        set(STATUS_COREAUDIO "OK")
        set(SOUNDIO_HAVE_COREAUDIO true)
        include_directories(${COREAUDIO_INCLUDE_DIR})

        find_path(COREFOUNDATION_INCLUDE_DIR NAMES CoreFoundation.h)
        find_library(COREFOUNDATION_LIBRARY NAMES CoreFoundation)
        include_directories(${COREFOUNDATION_INCLUDE_DIR})

        find_path(AUDIOUNIT_INCLUDE_DIR NAMES AudioUnit.h)
        find_library(AUDIOUNIT_LIBRARY NAMES AudioUnit)
        include_directories(${AUDIOUNIT_INCLUDE_DIR})
    else()
        set(STATUS_COREAUDIO "not found")
        set(SOUNDIO_HAVE_COREAUDIO false)
        set(COREAUDIO_LIBRARY "")
        set(COREFOUNDATION_LIBRARY "")
        set(AUDIOUNIT_LIBRARY "")
    endif()
else()
    set(STATUS_COREAUDIO "disabled")
    set(SOUNDIO_HAVE_COREAUDIO false)
    set(COREAUDIO_LIBRARY "")
    set(COREFOUNDATION_LIBRARY "")
    set(AUDIOUNIT_LIBRARY "")
endif()

if(ENABLE_WASAPI)
    find_package(WASAPI)
    if(WASAPI_FOUND)
        set(STATUS_WASAPI "OK")
        set(SOUNDIO_HAVE_WASAPI true)
    else()
        set(STATUS_WASAPI "not found")
        set(SOUNDIO_HAVE_WASAPI false)
    endif()
else()
    set(STATUS_WASAPI "disabled")
    set(SOUNDIO_HAVE_WASAPI false)
endif()

message(
    "System Dependencies\n"
    "-------------------\n"
    "* threads                      : ${STATUS_THREADS}\n"
    "* JACK       (optional)        : ${STATUS_JACK}\n"
    "* PulseAudio (optional)        : ${STATUS_PULSEAUDIO}\n"
    "* ALSA       (optional)        : ${STATUS_ALSA}\n"
    "* CoreAudio  (optional)        : ${STATUS_COREAUDIO}\n"
    "* WASAPI     (optional)        : ${STATUS_WASAPI}\n"
)

set(LIBSOUNDIO_SOURCES
    "./src/soundio.c"
    "./src/util.c"
    "./src/os.c"
    "./src/dummy.c"
    "./src/channel_layout.c"
    "./src/ring_buffer.c"
)

if(SOUNDIO_HAVE_JACK)
    set(LIBSOUNDIO_SOURCES ${LIBSOUNDIO_SOURCES}
        "./src/jack.c"
    )
endif()
if(SOUNDIO_HAVE_PULSEAUDIO)
    set(LIBSOUNDIO_SOURCES ${LIBSOUNDIO_SOURCES}
        "./src/pulseaudio.c"
    )
endif()
if(SOUNDIO_HAVE_ALSA)
    set(LIBSOUNDIO_SOURCES ${LIBSOUNDIO_SOURCES}
        "./src/alsa.c"
    )
endif()
if(SOUNDIO_HAVE_COREAUDIO)
    set(LIBSOUNDIO_SOURCES ${LIBSOUNDIO_SOURCES}
        "./src/coreaudio.c"
    )
endif()
if(SOUNDIO_HAVE_WASAPI)
    set(LIBSOUNDIO_SOURCES ${LIBSOUNDIO_SOURCES}
        "./src/wasapi.c"
    )
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h"
)

include_directories(
    ./include
)

set(LIBSOUNDIO_LIBS
    ${JACK_LIBRARY}
    ${PULSEAUDIO_LIBRARY}
    ${ALSA_LIBRARIES}
    ${COREAUDIO_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${AUDIOUNIT_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)


set(LIB_CFLAGS "-std=c11 -fvisibility=hidden -Wall -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes -D_REENTRANT -D_POSIX_C_SOURCE=200809L -Wno-missing-braces")

add_library(soundio_static STATIC ${LIBSOUNDIO_SOURCES})
target_link_libraries(soundio_static LINK_PUBLIC ${LIBSOUNDIO_LIBS} m)
set_target_properties(soundio_static PROPERTIES
        COMPILE_FLAGS ${LIB_CFLAGS}
        LINKER_LANGUAGE C
    )