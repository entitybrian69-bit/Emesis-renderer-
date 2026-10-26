cmake_minimum_required(VERSION 3.16)
project(emesis_modern VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

add_library(emesis_modern SHARED
    basevertex.c
    blending.c
    egl.c
    framebuffer.c
    env.c
    glformats.c
    license_notice.c
    main.c
    multidraw.c
    of_buffer_copier.c
    proc.c
    query.c
    shader_wrapper.c
    string_utils.c
    stubs.c
    swizzle.c
    vertexattrib.c
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(emesis_modern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set(MODERN_OLD_CMAKE_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(unordered_map)
add_subdirectory(glsl_optimizer)
set(CMAKE_SOURCE_DIR "${MODERN_OLD_CMAKE_SOURCE_DIR}")

target_compile_options(emesis_modern PRIVATE -O3 -ffunction-sections -fdata-sections -fPIC)
target_link_options(emesis_modern PRIVATE
    -Wl,-Bsymbolic
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script"
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
    -Wl,--strip-all
)
target_link_libraries(emesis_modern PRIVATE unordered_map glsl_optimizer)
if(ANDROID)
    target_link_libraries(emesis_modern PRIVATE log dl m)
endif()
