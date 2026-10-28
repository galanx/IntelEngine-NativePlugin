cmake_minimum_required(VERSION 3.21)
project(LegacyCacheMigration LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "Copy-only transaction requires Windows file/share semantics")
endif()
enable_language(RC)
# Configure this directory independently of the game DLL, using the existing
# vcpkg nlohmann_json package via CMAKE_PREFIX_PATH. No game ABI or new dependency.
find_package(nlohmann_json CONFIG REQUIRED)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/../src/RenderedCacheCodec.h" CACHE_CODEC_SHA256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "../src/RenderedCacheCodec.h")
foreach(target IN ITEMS LegacyCacheMigration LegacyCacheMigrationCheck)
    if(target STREQUAL "LegacyCacheMigration")
        set(entry LegacyCacheMigration.cpp)
    else()
        set(entry ../tests/LegacyCacheMigrationCheck.cpp)
    endif()
    add_executable(${target} ${entry} LegacyCacheMigrationIO.cpp)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    target_compile_definitions(${target} PRIVATE LCM_CODEC_SHA256="${CACHE_CODEC_SHA256}")
    target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json bcrypt shell32)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
        set_target_properties(${target} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            MSVC_DEBUG_INFORMATION_FORMAT "")
        target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/DEBUG:NONE>"
            "$<$<CONFIG:Release>:/INCREMENTAL:NO>")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
target_sources(LegacyCacheMigration PRIVATE LegacyCacheMigration.rc)
add_custom_command(TARGET LegacyCacheMigration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Convert-IE-Save.bat"
        "$<TARGET_FILE_DIR:LegacyCacheMigration>/Convert-IE-Save.bat")
