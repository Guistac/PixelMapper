
message(STATUS "== Including ArtNode")

set(ARTNODE_DIRECTORY ${DEPENDENCIES_DIRECTORY}/artnode/src)

set(ARTNODE_SOURCES
    ${ARTNODE_DIRECTORY}/Art-Net.h
    ${ARTNODE_DIRECTORY}/Art-NetOemCodes.h
    ${ARTNODE_DIRECTORY}/ArtDmx.h
    ${ARTNODE_DIRECTORY}/ArtNode.cpp
    ${ARTNODE_DIRECTORY}/ArtNode.h
    ${ARTNODE_DIRECTORY}/ArtPollReply.h
    ${ARTNODE_DIRECTORY}/RDM.h
)

##source_group(TREE ${ARTNODE_DIRECTORY} FILES ${ARTNODE_SOURCES})

add_library(artnode ${ARTNODE_SOURCES})

target_include_directories(artnode PUBLIC ${ARTNODE_DIRECTORY} )

set_target_properties(artnode PROPERTIES LINKER_LANGUAGE CXX)