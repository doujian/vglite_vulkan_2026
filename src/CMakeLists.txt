add_library(vg_lite
    vg_lite.c
    vg_lite_vulkan.c
    vg_lite_format.c
    vlc_parser.c
    tessellator.c
    vg_lite_draw.c
    vg_lite_gradient.c
    shader_loader.c
)

target_include_directories(vg_lite PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_link_libraries(vg_lite PUBLIC Vulkan::Vulkan)
else()
    target_link_libraries(vg_lite PUBLIC Vulkan::Vulkan m)
endif()

# Ensure shaders are compiled before vg_lite
if(TARGET spirv_compilation)
    add_dependencies(vg_lite spirv_compilation)
endif()
