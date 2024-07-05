
macro(binarize RESOURCE_FILE RESOURCES_OBJ)
  set(BINARY_NAME _${RESOURCE_FILE})
  string(MAKE_C_IDENTIFIER  _binary_${CMAKE_CURRENT_SOURCE_DIR} SYM_NAME)
  string(MAKE_C_IDENTIFIER  ${BINARY_NAME} BINARY_NAME)

  set(BINARY_START ${BINARY_NAME}_start)
  set(BINARY_END ${BINARY_NAME}_end)

  add_custom_command(
      OUTPUT ${RESOURCES_OBJ} 
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target binary --output-target ${ARCH}
      --rename-section .data=.rodata,alloc,load,readonly,data,contents
      ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_FILE} ${RESOURCES_OBJ}
      COMMAND 
      ${CMAKE_OBJCOPY}
      --redefine-sym ${SYM_NAME}${BINARY_START}=${BINARY_START}
      --redefine-sym ${SYM_NAME}${BINARY_END}=${BINARY_END}
      ${RESOURCES_OBJ} ${RESOURCES_OBJ}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_FILE}
  )

  set_source_files_properties(
    ${RESOURCES_OBJ}
    PROPERTIES
    EXTERNAL_OBJECT true
    GENERATED true
  )
endmacro()

binarize(Hack-Regular.ttf font.obj)

add_library(resources_static STATIC font.obj)
set_target_properties(resources_static PROPERTIES LINKER_LANGUAGE C)
