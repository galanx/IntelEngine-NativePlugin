cmake_minimum_required(VERSION 3.21)

# =============================================================================
# Project Configuration
# =============================================================================

project(
    IntelEngine
    VERSION 3.5.0
    DESCRIPTION "Intelligent NPC Task Execution for SkyrimNet"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Dependencies (via vcpkg)
# =============================================================================

find_package(CommonLibSSE CONFIG REQUIRED)
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
    SOURCES ${SOURCES}
)

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

# Workaround for MSVC Internal Compiler Error (C1001) in heavy template code
# (CommonLibSSE Relocation.h / xstring). Safe to remove if MSVC is updated.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /d2ReducedOptimizeHugeFunctions)
endif()

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
