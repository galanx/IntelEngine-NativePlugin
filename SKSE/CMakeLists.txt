cmake_minimum_required(VERSION 3.21)

# =============================================================================
# Project Configuration
# =============================================================================

project(
    IntelEngine
    VERSION 4.0.0
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

# CommonLib comes from the overlay port in vcpkg-ports/commonlibsse-ng. vcpkg builds it once per machine and
# restores it from the binary cache in every worktree, instead of compiling it inside each build directory.
find_package(CommonLibSSE CONFIG REQUIRED)

# Provenance: the installed package must come from this repository's port, at its pinned revision, MIT-licensed.
set(_commonlib_port "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-ports/commonlibsse-ng/portfile.cmake")
set(_commonlib_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/commonlibsse-ng")
file(STRINGS "${_commonlib_port}" _commonlib_ref_line REGEX "^[ \t]*REF [0-9a-f]+[ \t]*$" LIMIT_COUNT 1)
string(REGEX REPLACE "^[ \t]*REF ([0-9a-f]+)[ \t]*$" "\\1" _commonlib_revision "${_commonlib_ref_line}")
string(LENGTH "${_commonlib_revision}" _commonlib_revision_length)
if(NOT _commonlib_revision_length EQUAL 40)
    message(FATAL_ERROR "The CommonLib port must pin a full commit SHA as REF; got '${_commonlib_revision}'")
endif()
file(SHA256 "${_commonlib_port}" _commonlib_port_sha)
file(STRINGS "${_commonlib_share}/vcpkg_abi_info.txt" _commonlib_abi_port REGEX "^portfile.cmake ")
if(NOT _commonlib_abi_port STREQUAL "portfile.cmake ${_commonlib_port_sha}")
    message(FATAL_ERROR "The installed CommonLib was not built from vcpkg-ports/commonlibsse-ng (installed: '${_commonlib_abi_port}')")
endif()
file(STRINGS "${_commonlib_share}/copyright" _commonlib_license LIMIT_COUNT 1)
if(NOT _commonlib_license STREQUAL "MIT License")
    message(FATAL_ERROR "The pinned CommonLib must retain its MIT license")
endif()
get_target_property(_commonlib_definitions CommonLibSSE::CommonLibSSE INTERFACE_COMPILE_DEFINITIONS)
if(NOT _commonlib_definitions MATCHES "ENABLE_SKYRIM_VR=1")
    # A flat-only build would need the TESObjectREFR::Unk_8C header correction, which a prebuilt library cannot take.
    message(FATAL_ERROR "IntelEngine requires CommonLib built with ENABLE_SKYRIM_VR")
endif()
message(STATUS "CommonLib provenance: FatelessAI/CommonLibVR ${_commonlib_revision}, MIT, vcpkg port")

# Cross-VR uses non-virtual runtime wrappers (getter slots 0x97 flat / 0x98 VR). Keep the fingerprint guard:
# a changed header means the ABI assumptions in this plugin must be re-audited.
file(READ "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/RE/T/TESObjectREFR.h" _commonlib_refr_header)
string(REPLACE "\r\n" "\n" _commonlib_refr_header "${_commonlib_refr_header}")
string(SHA256 _commonlib_refr_sha "${_commonlib_refr_header}")
if(NOT _commonlib_refr_sha STREQUAL "98a8a1cb24f51b893b4343c4c552e363d9bc90ee7a611c05f7719c2479bec2b6")
    message(FATAL_ERROR "The audited CommonLib TESObjectREFR header changed; re-evaluate the non-VR ABI correction")
endif()

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
    src/NaturalTimeParser.cpp
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
    # Unmodified Magelight v1.7.1 API header from https://github.com/Severause/MagelightUI/blob/v1.7.1/api/MagelightUI_API.h
    # v1.7.1 peeled commit: 05d05d8cf13164e0dead3596640f28c058b9b307; upstream LICENSE is MIT (Copyright (c) 2026 Severause).
    src/UiHost.cpp
    src/DashboardUIManager.cpp
    src/PoliticalDB.cpp
    src/FactionPolitics.cpp
    src/FactionConfigLoader.cpp
    src/FactionEditor.cpp
    src/BattleManager.cpp
    src/DialogueTracker.cpp
    src/SpeechTracker.cpp
    src/LanguageBridge.cpp
    src/LivingWorldBridge.cpp
    src/LivingWorldLives.cpp
    src/PlaceTargets.cpp
    src/StorylineBridge.cpp
    src/QuestBridge.cpp
    src/ArcBridge.cpp
    src/TaleAuthor.cpp
    src/HeartBridge.cpp
    src/BondBridge.cpp
    src/RumorBridge.cpp
    src/StoryDirector.cpp
    src/SettingsBridge.cpp
    src/AsyncDispatch.cpp
    src/PoliticalEngineBridge.cpp
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
add_executable(DashboardUtf8DumpCheck EXCLUDE_FROM_ALL tests/DashboardUtf8DumpCheck.cpp)
target_link_libraries(DashboardUtf8DumpCheck PRIVATE nlohmann_json::nlohmann_json)
add_executable(StorylinesCheck EXCLUDE_FROM_ALL tests/StorylinesCheck.cpp)
target_include_directories(StorylinesCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(QuestsCheck EXCLUDE_FROM_ALL tests/QuestsCheck.cpp)
target_include_directories(QuestsCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(ArcsCheck EXCLUDE_FROM_ALL tests/ArcsCheck.cpp)
target_include_directories(ArcsCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(HeartsCheck EXCLUDE_FROM_ALL tests/HeartsCheck.cpp)
target_include_directories(HeartsCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(TalesCheck EXCLUDE_FROM_ALL tests/TalesCheck.cpp)
target_include_directories(TalesCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(TalesCheck PRIVATE nlohmann_json::nlohmann_json)
add_executable(LivingWorldCheck EXCLUDE_FROM_ALL tests/LivingWorldCheck.cpp)
target_include_directories(LivingWorldCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# What passed while apart, told: reunions, adventures' endings, keepsakes, the talk of the day (LivingWorld.h, Rumors.h).
add_executable(LivesToldCheck EXCLUDE_FROM_ALL tests/LivesToldCheck.cpp)
target_include_directories(LivesToldCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(PlaceNamesCheck EXCLUDE_FROM_ALL tests/PlaceNamesCheck.cpp)
target_include_directories(PlaceNamesCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(ItemNamesCheck EXCLUDE_FROM_ALL tests/ItemNamesCheck.cpp)
target_include_directories(ItemNamesCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The game's text and ours (GameText.h, LetterFold.h) and words of place as data (PlaceWords.h); both files are UTF-8.
add_executable(GameTextCheck EXCLUDE_FROM_ALL tests/GameTextCheck.cpp)
target_include_directories(GameTextCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(GameTextCheck PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_executable(PlaceWordsCheck EXCLUDE_FROM_ALL tests/PlaceWordsCheck.cpp)
target_include_directories(PlaceWordsCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(PlaceWordsCheck PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_executable(StoryDirectorCheck EXCLUDE_FROM_ALL tests/StoryDirectorCheck.cpp)
target_include_directories(StoryDirectorCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(StoryCandidateResolutionCheck EXCLUDE_FROM_ALL tests/StoryCandidateResolutionCheck.cpp)
target_include_directories(StoryCandidateResolutionCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(NamedPlaceMatchCheck EXCLUDE_FROM_ALL tests/NamedPlaceMatchCheck.cpp)
target_include_directories(NamedPlaceMatchCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(NamedPlaceMatchCheck PRIVATE nlohmann_json::nlohmann_json)
add_executable(LocalizedNameCheck EXCLUDE_FROM_ALL tests/LocalizedNameCheck.cpp)
target_include_directories(LocalizedNameCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(FactionLeaderNameMatchCheck EXCLUDE_FROM_ALL tests/FactionLeaderNameMatchCheck.cpp)
target_include_directories(FactionLeaderNameMatchCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(FormIDStringCheck EXCLUDE_FROM_ALL tests/FormIDStringCheck.cpp)
target_include_directories(FormIDStringCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(DirectorActorResolutionCheck EXCLUDE_FROM_ALL tests/DirectorActorResolutionCheck.cpp)
target_include_directories(DirectorActorResolutionCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(FactionPairValidationCheck EXCLUDE_FROM_ALL tests/FactionPairValidationCheck.cpp)
target_include_directories(FactionPairValidationCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(InformantRumorValidationCheck EXCLUDE_FROM_ALL tests/InformantRumorValidationCheck.cpp)
target_include_directories(InformantRumorValidationCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(TaskHistoryDescriptionCheck EXCLUDE_FROM_ALL tests/TaskHistoryDescriptionCheck.cpp)
target_include_directories(TaskHistoryDescriptionCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Offline synthetic engine vtables exercise the production CommonLib getter.
add_executable(ReferenceGetterDispatchCheck EXCLUDE_FROM_ALL tests/ReferenceGetterDispatchCheck.cpp)
target_compile_definitions(ReferenceGetterDispatchCheck PRIVATE ENABLE_COMMONLIBSSE_TESTING=1)
target_link_libraries(ReferenceGetterDispatchCheck PRIVATE CommonLibSSE::CommonLibSSE)

add_executable(ProximityDistanceCheck EXCLUDE_FROM_ALL tests/ProximityDistanceCheck.cpp)
target_include_directories(ProximityDistanceCheck PRIVATE src)
add_executable(NaturalTimeParserCheck EXCLUDE_FROM_ALL tests/NaturalTimeParserCheck.cpp src/NaturalTimeParser.cpp)
target_include_directories(NaturalTimeParserCheck PRIVATE src)

add_executable(PoliticalEngineReadJSONCheck EXCLUDE_FROM_ALL tests/PoliticalEngineReadJSONCheck.cpp)
target_include_directories(PoliticalEngineReadJSONCheck PRIVATE src)
target_link_libraries(PoliticalEngineReadJSONCheck PRIVATE nlohmann_json::nlohmann_json)

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
