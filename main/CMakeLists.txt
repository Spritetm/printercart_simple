set(srcs "main.c" "read_picture.c" "local_io.c"
    )

idf_component_register(SRCS ${srcs} EMBED_FILES picture.rgb
                    INCLUDE_DIRS ".")

add_custom_command(OUTPUT picture.rgb
     COMMAND convert ${COMPONENT_DIR}/picture.png picture.rgb
     DEPENDS ${COMPONENT_DIR}/picture.png
     VERBATIM)
add_custom_target(picture DEPENDS picture.rgb)
add_dependencies(${COMPONENT_LIB} picture)

set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY
     ADDITIONAL_MAKE_CLEAN_FILES picture.rgb)


