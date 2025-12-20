
message(STATUS "== Including GLM")

set( GLM_TEST_ENABLE      OFF CACHE INTERNAL "" )
set( BUILD_SHARED_LIBS    OFF CACHE INTERNAL "" )
set( BUILD_STATIC_LIBS    OFF CACHE INTERNAL "" )

add_library(glm INTERFACE)

target_compile_definitions(glm INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_include_directories(glm INTERFACE
    ${DEPENDENCIES_DIRECTORY}/glm
)

set_property(TARGET glm PROPERTY FOLDER "${legatoMathFolder}")