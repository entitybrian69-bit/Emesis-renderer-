cmake_minimum_required(VERSION 3.16)
project(emesis_router LANGUAGES C)

set(CMAKE_C_STANDARD 11)

add_library(emesis_renderer SHARED
    mc_version_detector.c
    emesis_router.c
)

target_include_directories(emesis_renderer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../emesis_modern"
)

target_compile_options(emesis_renderer PRIVATE
    -O3
    -fPIC
    -ffunction-sections
    -fdata-sections
    -fno-math-errno
    -fno-trapping-math
)

target_link_options(emesis_renderer PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script"
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
    -Wl,--strip-all
)

target_link_libraries(emesis_renderer PRIVATE log dl z m)
