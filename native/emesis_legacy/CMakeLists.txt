cmake_minimum_required(VERSION 3.16)
project(emesis_legacy LANGUAGES C CXX)

set(ENABLE_RTTI                ON  CACHE BOOL "" FORCE)
set(ENABLE_PCH                 OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "" FORCE)
set(ENABLE_HLSL                OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL             OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES    OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT                 OFF CACHE BOOL "" FORCE)

set(SPIRV_CROSS_ENABLE_C_API   ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL    ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL    OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL     OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP     OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI            OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC         ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL    OFF CACHE BOOL "" FORCE)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(DEFINED EMESIS_EXT_DEPS_DIR AND EXISTS "${EMESIS_EXT_DEPS_DIR}/glslang/CMakeLists.txt")
    add_subdirectory("${EMESIS_EXT_DEPS_DIR}/glslang" "${CMAKE_CURRENT_BINARY_DIR}/glslang_build" EXCLUDE_FROM_ALL)
    add_subdirectory("${EMESIS_EXT_DEPS_DIR}/SPIRV-Cross" "${CMAKE_CURRENT_BINARY_DIR}/spirv_cross_build" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        7099c123729e02f81d70559e79ee4360096fdfe5
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(
        spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        adec7acbf41a988713cdb85f93f26c8ca5ea863e
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glslang spirv_cross)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB Legacy_C_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/string_utils.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/cJSON.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/config.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/arbconverter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/arbgenerator.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/arbhelper.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/arbparser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/array.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/blend.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/blit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/buffers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/build_info.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/debug.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/decompress.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/depth.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/directstate.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/drawing.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/enable.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/envvars.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/eval.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/face.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/fog.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/fpe.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/fpe_cache.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/fpe_shader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/framebuffers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/gl_lookup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/gl4es.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/glstate.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/hint.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/init.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/light.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/line.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/list.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/listdraw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/listrl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/loader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/logs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/matrix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/matvec.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/oldprogram.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/pixel.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/planes.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/pointsprite.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/preproc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/program.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/queries.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/raster.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/render.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/shader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/shader_cache.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/shaderconv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/shader_hacks.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/stack.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/stencil.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/stubs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texenv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texgen.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texture.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texture_compressed.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texture_params.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texture_read.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/texture_3d.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/uniform.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/vertexattrib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/wrap/gl4eswraps.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/wrap/gles.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/wrap/glstub.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/wrap/glesnative.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/math/matheval.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glx/hardext.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glx/lookup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glx/gbm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glx/streaming.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/vgpu/shaderconv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/samplers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/glsl/glsl_for_es.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gl/getter.cpp"
)

add_library(emesis_legacy SHARED ${Legacy_C_SRC})

target_include_directories(emesis_legacy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(emesis_legacy PRIVATE
    NO_GBM
    DEFAULT_ES=3
    NOX11
    ANDROID
)

target_compile_options(emesis_legacy PRIVATE
    -O3
    -g0
    -fPIC
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -Wno-date-time
)

target_link_options(emesis_legacy PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script"
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
    -Wl,--strip-all
)

target_link_libraries(emesis_legacy PRIVATE
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    spirv-cross-c
    spirv-cross-glsl
    spirv-cross-core
    android
    log
    dl
    m
)
