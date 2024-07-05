set(RESOURCES_FONT_OBJ font.obj)

string(REPLACE ":" "_" SYM_NAME ${CMAKE_CURRENT_SOURCE_DIR})
string(REPLACE "/" "_" SYM_NAME ${SYM_NAME})

add_custom_command(
    OUTPUT ${RESOURCES_FONT_OBJ} 
    COMMAND ${CMAKE_OBJCOPY}
    --input-target binary --output-target ${ARCH}
    --rename-section .data=.rodata,alloc,load,readonly,data,contents
    ${CMAKE_CURRENT_SOURCE_DIR}/Hack-Regular.ttf ${RESOURCES_FONT_OBJ}
    COMMAND objcopy --wildcard
    --redefine-sym _binary_${SYM_NAME}_Hack_Regular_ttf_start=_Hack_Regular_ttf_start
    --redefine-sym _binary_${SYM_NAME}_Hack_Regular_ttf_end=_Hack_Regular_ttf_end
    ${RESOURCES_FONT_OBJ} ${RESOURCES_FONT_OBJ}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Hack-Regular.ttf
)

set_source_files_properties(
  ${RESOURCES_FONT_OBJ}
  PROPERTIES
  EXTERNAL_OBJECT true
  GENERATED true
)

add_library(resources_static STATIC ${RESOURCES_FONT_OBJ})
set_target_properties(resources_static PROPERTIES LINKER_LANGUAGE C)
