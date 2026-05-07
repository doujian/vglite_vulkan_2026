cmake_minimum_required(VERSION 3.16)
project(vglite_by_vulkan C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(-Wall -Wextra -Wno-unused-parameter -fPIC)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Find glslangValidator
find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found - SPIR-V compilation will be skipped")
endif()

# SPIR-V compilation for shaders
if(GLSLANG_VALIDATOR AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    file(GLOB VERT_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert")
    file(GLOB FRAG_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")

    foreach(VERT ${VERT_SHADERS})
        get_filename_component(NAME ${VERT} NAME_WE)
        set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_vert.spv")
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${GLSLANG_VALIDATOR} -V ${VERT} -o ${SPV_OUT}
            DEPENDS ${VERT}
            COMMENT "Compiling ${VERT} to SPIR-V"
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUT})
    endforeach()

    foreach(FRAG ${FRAG_SHADERS})
        get_filename_component(NAME ${FRAG} NAME_WE)
        set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_frag.spv")
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${GLSLANG_VALIDATOR} -V ${FRAG} -o ${SPV_OUT}
            DEPENDS ${FRAG}
            COMMENT "Compiling ${FRAG} to SPIR-V"
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUT})
    endforeach()

    if(SPV_OUTPUTS)
        add_custom_target(spirv_compilation ALL DEPENDS ${SPV_OUTPUTS})
    endif()
endif()

# Subdirectories
add_subdirectory(third_party/stb)
add_subdirectory(src)
add_subdirectory(util)
add_subdirectory(tests)
