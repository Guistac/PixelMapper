message(STATUS "== Including ImGuiColorTextEdit")

set(TEXTEDIT_DIRECTORY ${DEPENDENCIES_DIRECTORY}/ImGuiColorTextEdit)

set(TEXTEDIT_SOURCE_FILES
    ${TEXTEDIT_DIRECTORY}/TextEditor.cpp
    ${TEXTEDIT_DIRECTORY}/TextEditor.h
    ${TEXTEDIT_DIRECTORY}/TextDiff.cpp
    ${TEXTEDIT_DIRECTORY}/TextDiff.h
)

source_group(TREE ${TEXTEDIT_DIRECTORY} FILES ${TEXTEDIT_SOURCE_FILES})

add_library(imguitextedit STATIC ${TEXTEDIT_SOURCE_FILES})

target_include_directories(imguitextedit PUBLIC ${TEXTEDIT_DIRECTORY})

target_link_libraries(imguitextedit PUBLIC dearimgui)
