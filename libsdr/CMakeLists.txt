include_directories( .  ${LIBUSB_1_INCLUDE_DIRS} ../libutils/include ./include ../libimgui/include)

add_library(rtlsdr_static STATIC src/librtlsdr.c src/tuner_e4k.c src/tuner_fc0012.c src/tuner_fc0013.c src/tuner_fc2580.c src/tuner_r82xx.c src/scanner.cpp src/rtldev.cpp)
if(MINGW)
add_compile_definitions(rtlsdr_static _WIN32)
endif()
target_link_libraries(rtlsdr_static ${LIBUSB_1_LIBRARIES})
