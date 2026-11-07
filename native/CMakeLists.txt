cmake_minimum_required(VERSION 3.16)
project(EmesisRenderer VERSION 1.0.0 LANGUAGES C CXX)

option(EMESIS_BUILD_LEGACY "Build legacy engine" ON)

add_subdirectory(emesis_modern)
if(EMESIS_BUILD_LEGACY)
    add_subdirectory(emesis_legacy)
endif()
add_subdirectory(emesis_router)
