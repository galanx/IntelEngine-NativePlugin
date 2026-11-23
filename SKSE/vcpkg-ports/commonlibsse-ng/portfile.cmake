# Pinned CommonLib for IntelEngine. Built once per machine and restored from the vcpkg binary cache in every
# worktree. SKSE/CMakeLists.txt reads the REF below as the single source of truth for the pin.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FatelessAI/CommonLibVR
    REF 550cc4fb9114649dcf526d1f3d73d710c5d7003b
    SHA512 a053d852b682d597acc9e7684740ab95f212e79542265a3e379ff0132bb333592051ddaa2ffc81b1f85fdbda080afe360f5dc7a54d15d1aa7982a98865ce7243
    HEAD_REF ng
)

# extern/openvr is a submodule, which GitHub archives leave empty: fetch the commit the pin references.
vcpkg_from_github(
    OUT_SOURCE_PATH OPENVR_PATH
    REPO ValveSoftware/openvr
    REF 60eb187801956ad277f1cae6680e3a410ee0873b
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
    HEAD_REF master
)
file(GLOB _openvr_entries "${OPENVR_PATH}/*")
file(COPY ${_openvr_entries} DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DSKSE_SUPPORT_XBYAK=OFF
        -DENABLE_SKYRIM_SE=ON
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_VR=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)

# add_commonlibsse_plugin() lives here; the package config includes it.
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
file(INSTALL "${OPENVR_PATH}/lib/win64/openvr_api.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
# CommonLib's headers include <openvr.h>; the source build had extern/openvr/headers on the include path.
file(GLOB _openvr_headers "${OPENVR_PATH}/headers/*.h")
file(INSTALL ${_openvr_headers} DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# The exported targets carry the build tree's absolute openvr_api.lib path; point it into the package instead,
# so a package restored from the binary cache works in any worktree.
file(READ "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake" _targets)
string(REGEX REPLACE ":[A-Za-z]:[^;\"<>]*/openvr_api\\.lib" [[:${_IMPORT_PREFIX}/lib/openvr_api.lib]] _targets "${_targets}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake" "${_targets}")

# Upstream's config includes the targets before finding their dependencies and never finds DirectXTK, so
# consumers fail with "Microsoft::DirectXTK not found". Find every link dependency first.
file(WRITE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSEConfig.cmake" [[
include(CMakeFindDependencyMacro)
find_dependency(spdlog CONFIG)
find_dependency(directxtk CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE-targets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE.cmake")
]])

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
