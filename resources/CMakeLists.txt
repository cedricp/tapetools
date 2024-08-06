
macro(binarize RESOURCE_FILE RESOURCES_OBJ REDEF_SYM_PREFIX)
  set(BINARY_NAME _${RESOURCE_FILE})
  string(MAKE_C_IDENTIFIER  _binary_${CMAKE_CURRENT_SOURCE_DIR} SYM_NAME)
  string(MAKE_C_IDENTIFIER  ${BINARY_NAME} BINARY_NAME)

  set(BINARY_START ${BINARY_NAME}_start)
  set(BINARY_END ${BINARY_NAME}_end)
  set(BINARY_SIZE ${BINARY_NAME}_size)

  add_custom_command(
      OUTPUT ${RESOURCES_OBJ} 
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target binary --output-target ${ARCH}
      --rename-section .data=.rodata,alloc,load,readonly,data,contents
      --redefine-sym ${SYM_NAME}${BINARY_START}=${REDEF_SYM_PREFIX}_start
      --redefine-sym ${SYM_NAME}${BINARY_END}=${REDEF_SYM_PREFIX}_end
      --redefine-sym ${SYM_NAME}${BINARY_SIZE}=${REDEF_SYM_PREFIX}_size
      ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_FILE} ${RESOURCES_OBJ}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_FILE}
  )

  set_source_files_properties(
    ${RESOURCES_OBJ}
    PROPERTIES
    EXTERNAL_OBJECT true
    GENERATED true
  )
endmacro()

binarize(Hack-Regular.ttf font.obj _font_blob)

add_library(resources_static STATIC font.obj)
set_target_properties(resources_static PROPERTIES LINKER_LANGUAGE C)
