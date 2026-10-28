cmake_minimum_required(VERSION 3.21)

# =============================================================================
# Project Configuration
# =============================================================================

project(
    IntelEngine
    VERSION 3.6.0
    DESCRIPTION "Intelligent NPC Task Execution for SkyrimNet"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
    set(CMAKE_CXX_FLAGS_RELEASE "/Od /Ob0 /DNDEBUG" CACHE STRING "MSVC Release flags" FORCE)
endif()

# =============================================================================
# Dependencies
# =============================================================================

include(FetchContent)

set(
    INTELENGINE_COMMONLIBSSE_SOURCE_DIR
    ""
    CACHE PATH
    "Optional clean checkout of the pinned MinLL/CommonLibVR revision"
)

set(BUILD_TESTS OFF CACHE BOOL "Build CommonLibSSE tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build Skyrim AE support" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "Build Skyrim VR support" FORCE)
set(COMMONLIB_PREBUILT OFF CACHE BOOL "Use prebuilt CommonLib" FORCE)
set(COMMONLIB_PREBUILT_DIR "" CACHE PATH "Prebuilt CommonLib directory" FORCE)
set(_intelengine_commonlibsse_revision "550cc4fb9114649dcf526d1f3d73d710c5d7003b")

if(INTELENGINE_COMMONLIBSSE_SOURCE_DIR)
    add_subdirectory(
        "${INTELENGINE_COMMONLIBSSE_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/commonlibsse-ng"
    )
else()
    FetchContent_Declare(
        CommonLibSSE
        GIT_REPOSITORY https://github.com/MinLL/CommonLibVR.git
        GIT_TAG ${_intelengine_commonlibsse_revision}
        GIT_SUBMODULES extern/openvr
    )
    FetchContent_MakeAvailable(CommonLibSSE)
endif()

get_target_property(_intelengine_commonlibsse_source_dir CommonLibSSE SOURCE_DIR)

find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_intelengine_commonlibsse_source_dir}" rev-parse HEAD
    RESULT_VARIABLE _commonlib_revision_result
    OUTPUT_VARIABLE _commonlib_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _commonlib_revision_result EQUAL 0 OR
   NOT _commonlib_revision STREQUAL _intelengine_commonlibsse_revision)
    message(FATAL_ERROR "CommonLib must resolve to MinLL/CommonLibVR ${_intelengine_commonlibsse_revision}; got '${_commonlib_revision}'")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_intelengine_commonlibsse_source_dir}" status --porcelain --untracked-files=all
    RESULT_VARIABLE _commonlib_status_result
    OUTPUT_VARIABLE _commonlib_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _commonlib_status_result EQUAL 0 OR NOT _commonlib_status STREQUAL "")
    message(FATAL_ERROR "The pinned CommonLib checkout must be clean: ${_commonlib_status}")
endif()
file(STRINGS "${_intelengine_commonlibsse_source_dir}/LICENSE" _commonlib_license LIMIT_COUNT 1)
if(NOT _commonlib_license STREQUAL "MIT License")
    message(FATAL_ERROR "The pinned CommonLib checkout must retain its MIT license")
endif()
get_target_property(_commonlib_imported CommonLibSSE IMPORTED)
if(_commonlib_imported)
    message(FATAL_ERROR "IntelEngine requires source-built CommonLib, not a prebuilt library")
endif()
message(STATUS "CommonLib provenance: MinLL/CommonLibVR ${_commonlib_revision}, MIT, source-built")

# Cross-VR uses non-virtual runtime wrappers (getter slots 0x97 flat / 0x98 VR).
# Only flat-only builds need the extra Unk_8C virtual removed. Keep the
# fingerprint guard in every mode; both targets must use the same header view.
file(READ "${_intelengine_commonlibsse_source_dir}/include/RE/T/TESObjectREFR.h" _commonlib_refr_header)
string(REPLACE "\r\n" "\n" _commonlib_refr_header "${_commonlib_refr_header}")
string(SHA256 _commonlib_refr_sha "${_commonlib_refr_header}")
if(NOT _commonlib_refr_sha STREQUAL "98a8a1cb24f51b893b4343c4c552e363d9bc90ee7a611c05f7719c2479bec2b6")
    message(FATAL_ERROR "The audited CommonLib TESObjectREFR header changed; re-evaluate the non-VR ABI correction")
endif()
if(NOT ENABLE_SKYRIM_VR)
    set(_commonlib_abi_include "${CMAKE_CURRENT_BINARY_DIR}/commonlib-abi/include")
    string(REGEX REPLACE
        "\n[ \t]*SKYRIM_REL_VR_VIRTUAL void[ \t]+Unk_8C\\(void\\);[^\n]*"
        ""
        _commonlib_refr_fixed
        "${_commonlib_refr_header}"
    )
    if(_commonlib_refr_fixed STREQUAL _commonlib_refr_header)
        message(FATAL_ERROR "The CommonLib non-VR ABI correction did not remove the extra virtual slot")
    endif()
    file(MAKE_DIRECTORY "${_commonlib_abi_include}/RE/T")
    file(GENERATE OUTPUT "${_commonlib_abi_include}/RE/T/TESObjectREFR.h" CONTENT "${_commonlib_refr_fixed}")
    target_include_directories(CommonLibSSE BEFORE PUBLIC "$<BUILD_INTERFACE:${_commonlib_abi_include}>")
    message(STATUS "CommonLib ABI correction: omit VR-only TESObjectREFR::Unk_8C from SE/AE vtables")
else()
    message(STATUS "CommonLib ABI: pinned cross-VR runtime wrappers; no flat-only header override")
endif()

include("${_intelengine_commonlibsse_source_dir}/cmake/CommonLibSSE.cmake")

find_package(fmt CONFIG REQUIRED)
find_package(rapidfuzz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)


# =============================================================================
# Source Files
# =============================================================================

set(SOURCES
    src/Plugin.cpp
    src/Papyrus.cpp
    src/NPCIndex.cpp
    src/ItemIndex.cpp
    src/LocationResolver.cpp
    src/CellAnalyzer.cpp
    src/ActionValidator.cpp
    src/Settings.cpp
    src/StuckDetector.cpp
    src/DepartureDetector.cpp
    src/OffScreenTracker.cpp
    src/ProximityMonitor.cpp
    src/SlotTracker.cpp
    src/ProximityMonitor.cpp
    src/MemoryDB.cpp
    src/DashboardConfig.cpp
    src/DashboardUIManager.cpp
    src/PoliticalDB.cpp
    src/FactionPolitics.cpp
    src/FactionConfigLoader.cpp
    src/BattleManager.cpp
    src/DialogueTracker.cpp
    src/AsyncDispatch.cpp
)

# =============================================================================
# Target
# =============================================================================

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Galanx"
    USE_ADDRESS_LIBRARY
    SOURCES ${SOURCES}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        fmt::fmt
        rapidfuzz::rapidfuzz
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3
)

target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE
        src/Plugin.h
)

add_executable(Utf8TruncateCheck EXCLUDE_FROM_ALL tests/Utf8TruncateCheck.cpp)
target_include_directories(Utf8TruncateCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Offline synthetic engine vtables exercise the production CommonLib getter.
add_executable(ReferenceGetterDispatchCheck EXCLUDE_FROM_ALL tests/ReferenceGetterDispatchCheck.cpp)
target_compile_definitions(ReferenceGetterDispatchCheck PRIVATE ENABLE_COMMONLIBSSE_TESTING=1)
target_link_libraries(ReferenceGetterDispatchCheck PRIVATE CommonLibSSE::CommonLibSSE)


# =============================================================================
# Output Configuration
# =============================================================================

# Copy to Skyrim Data folder if environment variable is set
if(DEFINED ENV{SKYRIM_FOLDER})
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data/SKSE/Plugins")
elseif(DEFINED ENV{SKYRIM_MODS_FOLDER})
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/IntelEngine/SKSE/Plugins")
endif()

if(DEFINED OUTPUT_FOLDER)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${OUTPUT_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying ${PROJECT_NAME} to ${OUTPUT_FOLDER}"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:${PROJECT_NAME}>
                "${OUTPUT_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        )
    endif()
endif()
