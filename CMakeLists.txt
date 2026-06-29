cmake_minimum_required(VERSION 3.16)
project(vglite_by_vulkan C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -fPIC)
endif()

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

    set(SPV_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/spv")
    file(MAKE_DIRECTORY ${SPV_OUTPUT_DIR})

    message(STATUS "Shader compiler: ${GLSLANG_VALIDATOR}")
    message(STATUS "Shader source dir: ${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    message(STATUS "SPIR-V output dir: ${SPV_OUTPUT_DIR}")

    foreach(VERT ${VERT_SHADERS})
        get_filename_component(NAME ${VERT} NAME_WE)
        set(SPV_OUT "${SPV_OUTPUT_DIR}/${NAME}_vert.spv")
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling shader: ${VERT} -> ${SPV_OUT}"
            COMMAND ${GLSLANG_VALIDATOR} -V ${VERT} -o ${SPV_OUT}
            DEPENDS ${VERT}
            COMMENT "Vert SPIR-V: ${NAME}"
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUT})
    endforeach()

    foreach(FRAG ${FRAG_SHADERS})
        get_filename_component(NAME ${FRAG} NAME_WE)
        set(SPV_OUT "${SPV_OUTPUT_DIR}/${NAME}_frag.spv")
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling shader: ${FRAG} -> ${SPV_OUT}"
            COMMAND ${GLSLANG_VALIDATOR} -V ${FRAG} -o ${SPV_OUT}
            DEPENDS ${FRAG}
            COMMENT "Frag SPIR-V: ${NAME}"
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUT})
    endforeach()

    if(SPV_OUTPUTS)
        add_custom_target(spirv_compilation ALL DEPENDS ${SPV_OUTPUTS})
        list(LENGTH SPV_OUTPUTS SPV_COUNT)
        message(STATUS "Shaders to compile: ${SPV_COUNT} files")
    endif()
endif()

# Subdirectories
add_subdirectory(third_party/stb)
add_subdirectory(src)
add_subdirectory(util)
add_subdirectory(tests)
