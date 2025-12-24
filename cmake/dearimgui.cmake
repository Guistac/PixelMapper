
message(STATUS "== Including Dear ImGui")

set(IMGUI_DIRECTORY ${DEPENDENCIES_DIRECTORY}/dearimgui)

set(IMGUI_SOURCE_FILES
    ${IMGUI_DIRECTORY}/imconfig.h
    ${IMGUI_DIRECTORY}/imgui.h
    ${IMGUI_DIRECTORY}/imgui.cpp
    ${IMGUI_DIRECTORY}/imgui_draw.cpp
    ${IMGUI_DIRECTORY}/imgui_internal.h
    ${IMGUI_DIRECTORY}/imgui_widgets.cpp
    ${IMGUI_DIRECTORY}/imgui_tables.cpp
    ${IMGUI_DIRECTORY}/imstb_rectpack.h
    ${IMGUI_DIRECTORY}/imstb_textedit.h
    ${IMGUI_DIRECTORY}/imstb_truetype.h
    ${IMGUI_DIRECTORY}/imgui_demo.cpp
    
    ${IMGUI_DIRECTORY}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIRECTORY}/backends/imgui_impl_opengl3.h

    ${IMGUI_DIRECTORY}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIRECTORY}/backends/imgui_impl_glfw.h
)

source_group(TREE ${IMGUI_DIRECTORY} FILES ${IMGUI_SOURCE_FILES})


add_library(dearimgui ${IMGUI_SOURCE_FILES})

target_include_directories(dearimgui PUBLIC
    ${IMGUI_DIRECTORY}
    ${IMGUI_DIRECTORY}/backends

    #config file, used for casting between glm::vec2 and ImVec2
    ${IMGUI_DIRECTORY}/../../src/utils
)

# Make sure the directory containing your config file is in the include path
target_compile_definitions(dearimgui PUBLIC IMGUI_USER_CONFIG="imguiconfig.h")

target_link_libraries(dearimgui PUBLIC
    glfw
    glm
)
