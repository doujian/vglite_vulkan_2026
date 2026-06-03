add_library(vg_lite_util
    vg_lite_util.c
    util.c
)

target_include_directories(vg_lite_util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
)

if(MSVC)
    target_link_libraries(vg_lite_util PUBLIC
        vg_lite
        stb
    )
else()
    target_link_libraries(vg_lite_util PUBLIC
        vg_lite
        stb
        m
    )
endif()
