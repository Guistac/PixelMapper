
message(STATUS "== Including GLAD")

set(GLAD_DIRECTORY ${DEPENDENCIES_DIRECTORY}/glad)

set(GLAD_SOURCE_FILES
    ${GLAD_DIRECTORY}/include/glad/glad.h
	${GLAD_DIRECTORY}/include/KHR/khrplatform.h
	${GLAD_DIRECTORY}/src/glad.c
)

source_group(TREE ${GLAD_DIRECTORY} FILES ${GLAD_SOURCE_FILES})

add_library(glad ${GLAD_SOURCE_FILES})

target_include_directories(glad PUBLIC
	${GLAD_DIRECTORY}/include
)