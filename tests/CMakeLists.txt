include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/../util
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

if(MSVC)
    link_libraries(vg_lite vg_lite_util stb)
else()
    link_libraries(vg_lite vg_lite_util stb m)
endif()

add_executable(test_clear clear/clear.c)
add_dependencies(test_clear spirv_compilation)

add_executable(test_clear_unit Clear/Clear.c)
add_dependencies(test_clear_unit spirv_compilation)

add_executable(test_rotate rotate/rotate.c)
add_dependencies(test_rotate spirv_compilation)

add_executable(test_gfx21 gfx21/gfx21.c)
add_dependencies(test_gfx21 spirv_compilation)

add_executable(test_blend_premultiply blend_premultiply/blend_premultiply.c)
add_dependencies(test_blend_premultiply spirv_compilation)

add_executable(test_scale scale/scale.c)
add_dependencies(test_scale spirv_compilation)

add_executable(test_align16 align16/align16.c)
add_dependencies(test_align16 spirv_compilation)

add_executable(test_clear_dl clear_dl/clear_dl.c)
add_dependencies(test_clear_dl spirv_compilation)

add_executable(test_gfx1 gfx1/gfx1.c)
add_dependencies(test_gfx1 spirv_compilation)

add_executable(test_gfx2 gfx2/gfx2.c)
add_dependencies(test_gfx2 spirv_compilation)

add_executable(test_gfx3 gfx3/gfx3.c)
add_dependencies(test_gfx3 spirv_compilation)

add_executable(test_sft_clear sft_clear/sft_clear.c)
add_dependencies(test_sft_clear spirv_compilation)

add_executable(test_sft_blit sft_blit/sft_blit.c)
add_dependencies(test_sft_blit spirv_compilation)

add_executable(test_tiger tiger/tiger.c tiger/tiger_paths.h)
add_dependencies(test_tiger spirv_compilation)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_patternFill patternFill/patternFill.c)
add_dependencies(test_patternFill spirv_compilation)

add_executable(test_imgA8 imgA8/imgA8.c)
add_dependencies(test_imgA8 spirv_compilation)

add_executable(test_imgIndex imgIndex/imgIndex.c)
add_dependencies(test_imgIndex spirv_compilation)

add_executable(test_draw_image draw_image/draw_image.c)
add_dependencies(test_draw_image spirv_compilation)

add_executable(test_recolor recolor/recolor.c)
add_dependencies(test_recolor spirv_compilation)

add_executable(test_tiled tiled/tiled.c)
add_dependencies(test_tiled spirv_compilation)

add_executable(test_linearGrad linearGrad/linearGrad.c)
add_dependencies(test_linearGrad spirv_compilation)

# add_executable(test_radialGrad radialGrad/radialGrad.c)
# add_dependencies(test_radialGrad spirv_compilation)

add_executable(test_gradient Gradient/gradient.c)
add_dependencies(test_gradient spirv_compilation)

add_executable(test_scissor scissor/scissor.c)
add_dependencies(test_scissor spirv_compilation)

