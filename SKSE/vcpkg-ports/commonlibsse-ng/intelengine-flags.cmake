# Loaded right after CommonLib's project() call (CMAKE_PROJECT_INCLUDE). The same MSVC settings SKSE/CMakeLists.txt
# gives every target, which CommonLib got when it was built inside IntelEngine's configure: the port changes where
# CommonLib is built, not how it is compiled.
if(MSVC)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
    set(CMAKE_CXX_FLAGS_RELEASE "/Od /Ob0 /DNDEBUG" CACHE STRING "MSVC Release flags" FORCE)
endif()
