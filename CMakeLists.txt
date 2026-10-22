cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

set(PORT_NAME "Bellwood")
set(PORT_ARCHIVE "${PORT_NAME}.o2r")
set(GAME_ARCHIVE "ac.o2r")

# Set the project version and language
project(${PORT_NAME} VERSION 1.0.0 LANGUAGES C CXX)
include(FetchContent)

include(cmake/bellwood-cvars.cmake)
include(cmake/lus-cvars.cmake)

if(IOS)
    if(NOT DEFINED PLATFORM)
        set(PLATFORM "OS64")
    endif()
    option(IOS_SIGNING "Code sign the iOS app (OFF produces an unsigned bundle for sideloading)" ON)
    include(cmake/ios.toolchain.cmake)
    if(DEPLOYMENT_TARGET VERSION_LESS 16.3)
        message(FATAL_ERROR
            "iOS deployment target is ${DEPLOYMENT_TARGET}; ${PORT_NAME} needs 16.3 or newer.\n"
            "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ios.bellwood.toolchain.cmake, "
            "or pass -DDEPLOYMENT_TARGET=16.3 yourself.")
    endif()
    add_compile_definitions(
        PLATFORM_IOS=1
        __IOS__
    )
    if(IOS_SIGNING)
        set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "$ENV{IOS_DEVELOPMENT_TEAM}")
    endif()
    set(BUNDLE_ID "dev.net64.bellwood")
    set(SIGN_LIBRARY ${IOS_SIGNING})
    set(PROJECT_ID "${BUNDLE_ID}")
endif()

if(EMSCRIPTEN)
    add_compile_definitions(USE_OPENGLES=1)
    add_compile_options(-pthread -sUSE_SDL=2 -fexceptions -gline-tables-only)
    add_link_options(-pthread -sUSE_SDL=2)
endif()

if(ANDROID)
    add_compile_definitions(USE_OPENGLES=1)
endif()

option(XBOX_UWP "Build the Xbox Dev Mode target (UWP app container)" OFF)
if(XBOX_UWP)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "XBOX_UWP needs clang-cl: the game's struct layouts do not match under MSVC.\n"
            "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/uwp.bellwood.toolchain.cmake, which "
            "selects clang-cl from the Visual Studio LLVM component.")
    endif()
    add_compile_definitions(XBOX_UWP=1 __WINRT__=1)

    set(UWP_STORE_LIBDIR "")
    if(DEFINED ENV{VCToolsInstallDir})
        file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" _vctools)
        set(UWP_STORE_LIBDIR "${_vctools}/lib/x64/store")
    endif()
    if(NOT EXISTS "${UWP_STORE_LIBDIR}")
        file(GLOB _msvc_toolsets
            "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/lib/x64/store")
        list(SORT _msvc_toolsets ORDER DESCENDING)
        foreach(_candidate IN LISTS _msvc_toolsets)
            if(EXISTS "${_candidate}")
                set(UWP_STORE_LIBDIR "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT EXISTS "${UWP_STORE_LIBDIR}")
        message(FATAL_ERROR
            "App container CRT (lib/x64/store) not found. It comes with the Universal "
            "Windows Platform development workload. Configure from a shell where "
            "vcvars64.bat has been run, or pass -DUWP_STORE_LIBDIR=<path>.")
    endif()
    message(STATUS "App container CRT: ${UWP_STORE_LIBDIR}")
endif()

if(ANDROID OR IOS OR EMSCRIPTEN OR XBOX_UWP)
    set(BELLWOOD_CROSS_BUILD TRUE)
endif()

option(USE_SATELLA "Connect to Satella at startup for mod signing keys and online features" ON)

if(IOS OR EMSCRIPTEN OR XBOX_UWP OR NOT USE_SATELLA)
    set(USE_NETWORKING_DEFAULT OFF)
else()
    set(USE_NETWORKING_DEFAULT ON)
endif()
option(USE_NETWORKING "Build the Satella client and the online mod APIs" ${USE_NETWORKING_DEFAULT})
if(NOT USE_SATELLA AND USE_NETWORKING)
    set(USE_NETWORKING OFF CACHE BOOL "Disabled because USE_SATELLA is OFF" FORCE)
endif()
if(NOT USE_NETWORKING)
    message(STATUS "Satella is off: signed mods from Satella's keys need trusting by hand, and the relay and "
                   "store APIs do nothing.")
endif()

set(NATO_PHONETIC_ALPHABET
  "Alfa" "Bravo" "Charlie" "Delta" "Echo" "Foxtrot" "Golf" "Hotel"
  "India" "Juliett" "Kilo" "Lima" "Mike" "November" "Oscar" "Papa"
  "Quebec" "Romeo" "Sierra" "Tango" "Uniform" "Victor" "Whiskey"
  "Xray" "Yankee" "Zulu"
)

# Get the patch version number from the project version
math(EXPR PATCH_INDEX "${PROJECT_VERSION_PATCH}")

# Use the patch number to select the correct word
list(GET NATO_PHONETIC_ALPHABET ${PATCH_INDEX} PROJECT_PATCH_WORD)

set(PROJECT_BUILD_NAME "Nectarine ${PROJECT_PATCH_WORD}" CACHE STRING "" FORCE)
set(PROJECT_TEAM "Lywx" CACHE STRING "" FORCE)

# Get Git Information
execute_process(
    COMMAND git branch --show-current
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_PROJECT_GIT_BRANCH "${GIT_BRANCH}" CACHE STRING "Git branch" FORCE)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get only the first 7 characters of the hash
string(SUBSTRING "${GIT_COMMIT_HASH}" 0 7 SHORT_COMMIT_HASH)

set(CMAKE_PROJECT_GIT_COMMIT_HASH "${SHORT_COMMIT_HASH}" CACHE STRING "Git commit hash" FORCE)

execute_process(
    COMMAND git describe --tags --abbrev=0 --exact-match HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_COMMIT_TAG)
    set(GIT_COMMIT_TAG "" CACHE STRING "Git commit tag" FORCE)
endif()

set(CMAKE_PROJECT_GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

if(APPLE)
  enable_language(OBJCXX)
endif()

# Set the minimum version of CMake and the deployment target for macOS
if(NOT IOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
endif()

# Set the C++ standard and enable the MSVC parallel build option
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 11 CACHE STRING "The C standard to use")
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

# Add a custom module path to locate additional CMake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")
set(YAML_CPP_STATIC_DEFINE ON)
add_compile_definitions(YAML_CPP_STATIC_DEFINE)

if (WIN32)
include(cmake/automate-vcpkg.cmake)
if (XBOX_UWP)
    set(VCPKG_TRIPLET x64-uwp)
    set(VCPKG_TARGET_TRIPLET x64-uwp)
    vcpkg_bootstrap()
    vcpkg_install_packages(zlib bzip2 libzip libpng sdl2 nlohmann-json tinyxml2 spdlog mbedtls)
else()
    set(VCPKG_TRIPLET x64-windows-static)
    set(VCPKG_TARGET_TRIPLET x64-windows-static)
    vcpkg_bootstrap()
    vcpkg_install_packages(zlib bzip2 libzip libpng sdl2[vulkan] glew glfw3 nlohmann-json tinyxml2 spdlog mbedtls)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if (MSVC)
set(CPP "${CMAKE_C_COMPILER}" "/EP")
else()
set(CPP "${CMAKE_C_COMPILER}" "-E" "-P" "-Wno-trigraphs" "-x" "c")
endif()

################################################################################
# Sources
################################################################################
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/port/build.c.in ${CMAKE_CURRENT_BINARY_DIR}/src/port/build.c @ONLY)

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Global configuration types
################################################################################
if (NOT MSVC)
set(CMAKE_C_FLAGS_RELEASE "-O1 -fno-strict-aliasing -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O1 -DNDEBUG")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O1 -DNDEBUG")
endif()

add_compile_definitions(
    _LANGUAGE_C
    _USE_MATH_DEFINES
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT XBOX_UWP)
    add_compile_definitions(ENABLE_OPENGL=1)
endif()

# Find necessary libraries
if (UNIX AND NOT APPLE AND NOT BELLWOOD_CROSS_BUILD)
find_package(OpenGL REQUIRED)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/properties.h.in ${CMAKE_CURRENT_BINARY_DIR}/properties.h @ONLY)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

file(GLOB_RECURSE ALL_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    "src/port/*.h"
    "src/port/*.hpp"
    "src/port/*.c"
    "src/port/*.cpp"
)
file(GLOB_RECURSE PORT_GAME_C_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    "src/port/boot/*.c"
    "src/port/platform/*.c"
    "src/port/input/*.c"
    "src/port/stubs/*.c"
    "src/port/famicom/*.c"
    "src/port/events/*.c"
)
list(REMOVE_ITEM ALL_FILES ${PORT_GAME_C_FILES})
list(FILTER ALL_FILES EXCLUDE REGEX "^src/port/audio/.*\\.(c|cpp)$")
include(src/port/importer/ac/sources.cmake OPTIONAL)
if (AC_IMPORTER_SOURCES)
    list(FILTER ALL_FILES EXCLUDE REGEX "^src/port/importer/ac/")
    list(APPEND ALL_FILES ${AC_IMPORTER_SOURCES})
endif()

# Create source groups that match the real file directory paths
foreach(source IN LISTS ALL_FILES)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_adjusted "${source_path}")
    source_group("${source_path_adjusted}" FILES "${source}")
endforeach()

list(APPEND ALL_FILES ${CMAKE_CURRENT_BINARY_DIR}/src/port/build.c)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND ALL_FILES Resource.rc)
endif()

if (XBOX_UWP)
    set(UWP_PACKAGE_NAME "Bellwood" CACHE STRING "Appx package identity name")
    set(UWP_PACKAGE_PUBLISHER "CN=${PROJECT_TEAM}" CACHE STRING "Appx publisher (X.500 DN)")
    set(UWP_PACKAGE_VERSION_BASE "${PROJECT_VERSION}" CACHE STRING "Appx package version (w.x.y), build number appended")
    set(UWP_PACKAGE_GUID "00000000-0000-0000-0000-000000000001" CACHE STRING "Appx phone product id")
    set(UWP_PACKAGE_CERT_THUMBPRINT "" CACHE STRING "Thumbprint of a signing cert in the CurrentUser/My store")
    set(UWP_PACKAGE_CERT "" CACHE FILEPATH "PFX to sign the package with (alternative to the thumbprint)")
    set(UWP_PACKAGE_CERT_PASSWORD "" CACHE STRING "Password for UWP_PACKAGE_CERT, if it has one")

    set(UWP_LAYOUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/appx-layout")
    set(UWP_MANIFEST "${UWP_LAYOUT_DIR}/AppxManifest.xml")
    set(UWP_MANIFEST_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/src/port/uwp/Package.appxmanifest.in")
    set(UWP_BUILD_COUNTER "${CMAKE_CURRENT_BINARY_DIR}/uwp-build-number.txt")

    find_file(UWP_D3DCOMPILER d3dcompiler_47.dll
        PATHS
            "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/x64"
            "$ENV{ProgramFiles}/Windows Kits/10/Redist/D3D/x64"
        NO_DEFAULT_PATH
    )
    if (NOT UWP_D3DCOMPILER)
        message(FATAL_ERROR
            "d3dcompiler_47.dll (UWP D3D redist) not found. It ships with the Windows SDK at "
            "'Windows Kits/10/Redist/D3D/x64'. Install the SDK's D3D redist or pass "
            "-DUWP_D3DCOMPILER=<path to the UWP build of d3dcompiler_47.dll>.")
    endif()

    find_file(UWP_VCLIBS_APPX Microsoft.VCLibs.x64.14.00.appx
        PATHS
            "$ENV{ProgramFiles\(x86\)}/Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64"
            "$ENV{ProgramFiles}/Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64"
        NO_DEFAULT_PATH
    )
    if (UWP_VCLIBS_APPX)
        message(STATUS "VCLibs framework package: ${UWP_VCLIBS_APPX}")
    else()
        message(WARNING
            "Microsoft.VCLibs.x64.14.00.appx not found. The console needs this framework package "
            "or the app will not activate; it ships with the UWP workload under "
            "'Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs'.")
    endif()
endif()

if(IOS)
    set(IOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ios)
    set(IOS_RESOURCES ${IOS_DIR}/Launch.storyboard ${IOS_DIR}/PoweredBy.png ${IOS_DIR}/Icon.png)
    add_executable(${PROJECT_NAME} ${ALL_FILES} ${IOS_RESOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${IOS_DIR}/plist.in
        RESOURCE "${IOS_RESOURCES}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${BUNDLE_ID}"
    )
    if(IOS_SIGNING)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        )
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        )
    endif()
elseif(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${ALL_FILES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(${PROJECT_NAME} ${ALL_FILES})
endif()

if(EMSCRIPTEN)
    string(TOLOWER "${PORT_NAME}" PORT_WEB_NAME)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PORT_WEB_NAME}" SUFFIX ".html")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    F3DEX_GBI_2
    TARGET_PC
    VERSION=VER_GAFE01_00
    AVOID_UB=1
    PORT_NAME="${PORT_NAME}"
    PORT_ARCHIVE="${PORT_ARCHIVE}"
    GAME_ARCHIVE="${GAME_ARCHIVE}"
)

################################################################################
# MSVC runtime library
################################################################################
if (MSVC)
  if (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT XBOX_UWP)
	  get_property(MSVC_RUNTIME_LIBRARY_DEFAULT TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY)
	  if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
		  string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
			  $<$<CONFIG:Debug>:
                MultiThreadedDebug
			  >
			  $<$<CONFIG:Release>:
                MultiThreaded
			  >
			  $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
		  )
	  elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
		  string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
			  $<$<CONFIG:Debug>:
				  MultiThreadedDebug
			  >
			  $<$<CONFIG:Release>:
				  MultiThreaded
			  >
			  $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
		  )
	  endif()
	  set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR})
  endif()
endif()

#==============================================================================#
# Libultraship Integration                                                     #
#==============================================================================#

# Removes MPQ/OTR support
set(EXCLUDE_MPQ_SUPPORT TRUE CACHE BOOL "")
set(ENABLE_EXP_AUTO_CONFIGURE_CONTROLLERS ON CACHE BOOL "")
add_compile_definitions(EXCLUDE_MPQ_SUPPORT)

set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "GBI ucode version used to build libultraship" FORCE)

if(EMSCRIPTEN)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    set(tinyxml2_BUILD_TESTING OFF)
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 11.0.0
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.16.0
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE
    )

    file(REMOVE
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-config.cmake"
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-config-version.cmake"
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlibConfig.cmake"
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlibConfigVersion.cmake")
    set(_emsysroot "${EMSCRIPTEN_SYSROOT}")
    if(NOT _emsysroot)
        set(_emsysroot "${CMAKE_SYSROOT}")
    endif()
    if(NOT _emsysroot)
        message(FATAL_ERROR "Could not determine the Emscripten sysroot for zlib")
    endif()
    set(ZLIB_INCLUDE_DIR "${_emsysroot}/include" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY "${_emsysroot}/lib/wasm32-emscripten/libz.a" CACHE FILEPATH "" FORCE)

    set(BUILD_TOOLS OFF)
    set(BUILD_REGRESS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOC OFF)
    set(BUILD_OSSFUZZ OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_BZIP2 OFF)
    set(ENABLE_LZMA OFF)
    set(ENABLE_ZSTD OFF)
    set(ENABLE_COMMONCRYPTO OFF)
    set(ENABLE_GNUTLS OFF)
    set(ENABLE_MBEDTLS OFF)
    set(ENABLE_OPENSSL OFF)
    FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE
    )

    FetchContent_MakeAvailable(tinyxml2 nlohmann_json spdlog libzip)
endif()

if(ANDROID OR XBOX_UWP)
    set(ENABLE_SCRIPTING ON CACHE BOOL "" FORCE)
    set(DISABLE_DLL_LOADER ON CACHE BOOL "" FORCE)
elseif(BELLWOOD_CROSS_BUILD)
    set(ENABLE_SCRIPTING OFF CACHE BOOL "" FORCE)
    set(DISABLE_DLL_LOADER ON CACHE BOOL "" FORCE)
else()
    set(ENABLE_SCRIPTING ON CACHE BOOL "" FORCE)
endif()

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship
  ${SDL2_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIRS}
)

add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

if(ENABLE_SCRIPTING)
    set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS TRUE WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/libultraship/cmake/SetupTccRuntime.cmake)
    set(PROPERTY_READER_GUARD_DISABLED TRUE)
    if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
        set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "@executable_path")
        lus_setup_tcc_runtime(${PROJECT_NAME} RESOURCES_DIR "../Resources/.tcc")
    elseif(ANDROID)
        set(BELLWOOD_TCC_RUNTIME_DIR "${CMAKE_BINARY_DIR}/.tcc" CACHE PATH "Where the TCC runtime is staged for the APK")
        lus_setup_tcc_runtime(${PROJECT_NAME} STAGE_DIR "${BELLWOOD_TCC_RUNTIME_DIR}")
    else()
        lus_setup_tcc_runtime(${PROJECT_NAME})
    endif()
    unset(PROPERTY_READER_GUARD_DISABLED)
endif()
add_dependencies(${PROJECT_NAME} libultraship)
target_link_libraries(${PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,libultraship>")

option(BELLWOOD_ASAN "Build the port and the game with AddressSanitizer" OFF)

include(cmake/bellwood-game.cmake)
target_sources(acgame PRIVATE ${PORT_GAME_C_FILES})
include(cmake/bellwood-audio.cmake)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
if (BELLWOOD_ASAN AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
            "_DEBUG;"
            "_CRT_SECURE_NO_WARNINGS;"
            "ENABLE_DX11;"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG"
        ">"
        "INCLUDE_GAME_PRINTF;"
        "UNICODE;"
        "_UNICODE;"
        "_CRT_SECURE_NO_WARNINGS;"
        "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS;"
    )
    add_compile_definitions(STORMLIB_NO_AUTO_LINK)
	set(STORMLIB_NO_AUTO_LINK ON)
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|Clang|AppleClang")
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		"$<$<CONFIG:Debug>:"
			"_DEBUG"
		">"
		"$<$<CONFIG:Release>:"
			"NDEBUG"
		">"
		"SPDLOG_ACTIVE_LEVEL=0;"
		"_CONSOLE;"
		"_CRT_SECURE_NO_WARNINGS;"
		"UNICODE;"
		"_UNICODE"
	)
endif()

set(ADDITIONAL_LIBRARY_DEPENDENCIES)

#==============================================================================#
# Torch Integration                                                            #
#==============================================================================#

option(USE_STANDALONE "Build as a standalone executable" OFF)
option(BUILD_STORMLIB "Build with StormLib support" OFF)

option(BUILD_SM64 "Build with Super Mario 64 support" OFF)
option(BUILD_MK64 "Build with Mario Kart 64 support" OFF)
option(BUILD_SF64 "Build with Star Fox 64 support" OFF)
option(BUILD_PM64 "Build with Paper Mario support" OFF)
option(BUILD_FZERO "Build with F-Zero X support" OFF)
option(BUILD_BK64 "Build with Banjo Kazooie support" OFF)
option(BUILD_MARIO_ARTIST "Build with Mario Artist support" OFF)
option(BUILD_OOT "Build with Ocarina of Time support" OFF)
option(BUILD_NAUDIO "Build with NAudio support" ON)
set(BUILD_GAMECUBE ON CACHE BOOL "Build with GameCube disc input support")
set(BUILD_AC ON CACHE BOOL "Build with Animal Crossing support")
add_subdirectory(Torch)

list(APPEND ADDITIONAL_LIBRARY_DEPENDENCIES torch)
FetchContent_GetProperties(dr_libs)
target_include_directories(acaudio PRIVATE ${dr_libs_SOURCE_DIR})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Torch/CMakeLists.txt" TORCH_HAS_GAMECUBE_INPUT REGEX "GAMECUBE_SUPPORT")
if(BUILD_GAMECUBE AND TORCH_HAS_GAMECUBE_INPUT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TORCH_GAMECUBE_SUPPORT=1)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")
################################################################################
# Compile and link options
################################################################################
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /w;
            /Od
        >
        $<$<CONFIG:Release>:
            /Oi;
            /Gy;
            /W3
        >

        /wd4024 #different types for formal and actual parameter [x]. Usually seen along side 4047.
        /wd4047 #'uintptr_t' differs in levels of indirection from 'Gfx *'. Usually caused by passing a uintptr_t into a function expecting a pointer-types
        /wd4090 #'initializing': different 'const' qualifiers. Caused when initializing a non const struct member with a const char*.
        /wd4101 # unreferenced local variable. Things like stack pads.
        /wd4133 #incompatible types. Usually caused by passing a c string into a function that takes a pointer.
        /wd4244 #conversion from [a] to [b]. Used to warn when copying to smaller type without a cast.
        $<$<COMPILE_LANGUAGE:C>:/J>
        /sdl-;
        /permissive-;
        /MP;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING}
    )
  target_compile_options(${PROJECT_NAME} PRIVATE  $<$<CONFIG:Debug>:/ZI;>)
  target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /INCREMENTAL
        >
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF;
            /INCREMENTAL:NO;
            /FORCE:MULTIPLE
        >
        /MANIFEST:NO;
        /DEBUG;
        /SUBSYSTEM:WINDOWS
    )

  # Remove /RTC from msvc flags
  foreach (fentry
    CMAKE_C_FLAGS
    CMAKE_CXX_FLAGS
  )
    string (REGEX REPLACE "/RTC(su|[1su])" "" ${fentry} "${${fentry}}")
  endforeach(fentry)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    if (APPLE)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall -Wextra -Wno-error
            -Wno-return-type
            -Wno-unused-parameter
            -Wno-unused-function
            -Wno-unused-variable
            -Wno-missing-field-initializers
            -Wno-parentheses
            -Wno-narrowing
            -Wno-missing-braces
            -Wno-int-conversion
            $<$<COMPILE_LANGUAGE:C>:
                -Werror-implicit-function-declaration
                -Wno-incompatible-pointer-types
                -fno-strict-aliasing
                -fwrapv
                -funsigned-char
            >
            $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
            $<$<COMPILE_LANGUAGE:CXX>:
                -Wno-c++11-narrowing
                -Wno-deprecated-enum-enum-conversion
            >
            -pthread
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            -pthread
        )
    else()
      if(BELLWOOD_CROSS_BUILD)
        set(CPU_OPTION)
      elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(CPU_OPTION -msse2 -mfpmath=sse)
      elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(CPU_OPTION -mcpu=native)
      endif()

      target_compile_options(${PROJECT_NAME} PRIVATE
          -Wall -Wextra -Wno-error
          -Wno-unused-parameter
          -Wno-unused-function
          -Wno-unused-variable
          -Wno-missing-field-initializers
          -Wno-parentheses
          -Wno-narrowing
          -Wno-missing-braces
          -Wno-int-conversion
          $<$<COMPILE_LANGUAGE:C>:
              -Werror-implicit-function-declaration
              -Wno-incompatible-pointer-types
              -fno-strict-aliasing
              -fwrapv
              -funsigned-char
          >
          $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
          $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
          -pthread
    ${CPU_OPTION}
      )

      target_link_options(${PROJECT_NAME} PRIVATE -pthread)
      if(NOT EMSCRIPTEN)
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
      endif()
  endif()
endif()

if(IOS)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Bundling config.yml and asset yamls..."
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -DDST_DIR=$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>
            -P ${CMAKE_SOURCE_DIR}/cmake/copy-extractor-data.cmake
  )
elseif(NOT ANDROID AND NOT EMSCRIPTEN)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Copying asset yamls..."
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -DDST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
            -P ${CMAKE_SOURCE_DIR}/cmake/copy-extractor-data.cmake
  )
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLL dependencies next to ${PROJECT_NAME}.exe..."
  )
  find_file(PORT_SHADERC_DLL NAMES shaderc_shared.dll
            HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
  if (PORT_SHADERC_DLL)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PORT_SHADERC_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
      COMMENT "Copying shaderc_shared.dll next to ${PROJECT_NAME}.exe..."
    )
  endif()
endif()

set(PORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

if(USE_NETWORKING)
    if(APPLE)
        find_program(BREW_EXECUTABLE brew)
        if(BREW_EXECUTABLE)
            execute_process(COMMAND ${BREW_EXECUTABLE} --prefix mbedtls@3
                            OUTPUT_VARIABLE MBEDTLS3_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT EXISTS "${MBEDTLS3_PREFIX}/lib")
                set(MBEDTLS3_PREFIX "")
            endif()
            if(MBEDTLS3_PREFIX)
                list(APPEND CMAKE_PREFIX_PATH "${MBEDTLS3_PREFIX}")
            endif()
        endif()
    endif()
    if(ANDROID)
        FetchContent_Declare(
            mbedtls
            URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
            URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
        )
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(GEN_FILES OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(mbedtls)
        set(MBEDTLS_FOUND TRUE)
        set(MBEDTLS_VERSION_GREATER_THAN_3 TRUE)
        set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include")
        set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
        target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto)
    endif()
    FetchContent_Declare(
        ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG v11.4.5
    )
    set(USE_TLS ON CACHE BOOL "" FORCE)
    set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ixwebsocket)
    if(ANDROID)
        target_compile_definitions(ixwebsocket PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE ixwebsocket)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_NETWORKING BELLWOOD_VERSION="${PORT_VERSION_STRING}")
    if(APPLE)
        find_path(MBEDTLS_INCLUDE_DIR NAMES mbedtls/ctr_drbg.h HINTS "${MBEDTLS3_PREFIX}/include")
        if(NOT MBEDTLS_INCLUDE_DIR)
            message(FATAL_ERROR "mbedtls 2.x/3.x headers not found; install Homebrew's mbedtls@3 or MacPorts' mbedtls.")
        endif()
        get_filename_component(MBEDTLS_ROOT "${MBEDTLS_INCLUDE_DIR}" DIRECTORY)
        find_library(MBEDTLS_LIB mbedtls PATHS "${MBEDTLS_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(MBEDX509_LIB mbedx509 PATHS "${MBEDTLS_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(MBEDCRYPTO_LIB mbedcrypto PATHS "${MBEDTLS_ROOT}/lib" NO_DEFAULT_PATH)
        target_include_directories(${PROJECT_NAME} PRIVATE "${MBEDTLS_INCLUDE_DIR}")
        target_link_libraries(${PROJECT_NAME} PRIVATE ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
    endif()
endif()

set(PORT_O2R_STAGING ${CMAKE_BINARY_DIR}/port-o2r)
file(GLOB_RECURSE PORT_O2R_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/port/*"
    "${CMAKE_SOURCE_DIR}/libultraship/src/fast/shaders/*"
)

if(NOT BELLWOOD_CROSS_BUILD)
include(ExternalProject)
if (MSVC)
    set(TORCH_BUILD_COMMAND BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target torch)
endif()
ExternalProject_Add(TorchExternal
	PREFIX TorchExternal
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/Torch
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/TorchExternal/install
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SM64=OFF
        -DBUILD_MK64=OFF
        -DBUILD_SF64=OFF
        -DBUILD_PM64=OFF
        -DBUILD_FZERO=OFF
        -DBUILD_BK64=OFF
        -DBUILD_MARIO_ARTIST=OFF
        -DBUILD_OOT=OFF
        -DBUILD_NAUDIO=ON
        -DBUILD_GAMECUBE=${BUILD_GAMECUBE}
        -DBUILD_AC=${BUILD_AC}
        ${TORCH_BUILD_COMMAND}
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    EXCLUDE_FROM_ALL TRUE
)
ExternalProject_Get_Property(TorchExternal binary_dir)
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (_is_multi_config)
  set(TORCH_EXECUTABLE ${binary_dir}/$<CONFIG>/torch)
else()
  set(TORCH_EXECUTABLE ${binary_dir}/torch)
endif()

set(AC_DISC_PATH "" CACHE FILEPATH "GameCube disc image (.iso/.gcm/.ciso/.gcz) or extracted disc folder used by ExtractAssets")
if (AC_DISC_PATH)
  add_custom_target(
      ExtractAssets
      DEPENDS TorchExternal
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMAND ${TORCH_EXECUTABLE} o2r ${AC_DISC_PATH} -u ${PORT_VERSION_STRING}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${GAME_ARCHIVE}" "${CMAKE_BINARY_DIR}/${GAME_ARCHIVE}"
      VERBATIM
  )
else()
  add_custom_target(
      ExtractAssets
      COMMAND ${CMAKE_COMMAND} -E echo "ExtractAssets needs -DAC_DISC_PATH=/path/to/disc (image or extracted folder)"
      COMMAND ${CMAKE_COMMAND} -E false
      VERBATIM
  )
endif()

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}"
    DEPENDS TorchExternal ${PORT_O2R_INPUTS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PORT_O2R_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/libultraship/src/fast/shaders" "${PORT_O2R_STAGING}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/port" "${PORT_O2R_STAGING}"
    COMMAND ${TORCH_EXECUTABLE} pack "${PORT_O2R_STAGING}" "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" o2r -u ${PORT_VERSION_STRING}
    VERBATIM
)
else()
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}"
    DEPENDS ${PORT_O2R_INPUTS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PORT_O2R_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/libultraship/src/fast/shaders" "${PORT_O2R_STAGING}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/port" "${PORT_O2R_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PORT_O2R_STAGING}" ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" --format=zip -- .
    VERBATIM
)
endif()
add_custom_target(GeneratePortO2R DEPENDS "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}")
add_dependencies(${PROJECT_NAME} GeneratePortO2R)

if(XBOX_UWP)
    target_link_options(${PROJECT_NAME} PRIVATE
        /APPCONTAINER
        /SUBSYSTEM:CONSOLE
        "/LIBPATH:${UWP_STORE_LIBDIR}"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE WindowsApp.lib)

    set(UWP_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/port/uwp/Assets")

    file(GLOB UWP_RUNTIME_DLLS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    if(NOT UWP_RUNTIME_DLLS)
        message(WARNING
            "No vcpkg runtime DLLs found under '${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin'. "
            "The package will not start on the console if SDL2.dll is missing from it.")
    endif()

    get_filename_component(_sdk_bin "${UWP_D3DCOMPILER}" DIRECTORY)
    file(GLOB _sdk_bin_candidates "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*/x64")
    list(SORT _sdk_bin_candidates ORDER DESCENDING)
    find_program(UWP_MAKEAPPX makeappx HINTS ${_sdk_bin_candidates} NO_DEFAULT_PATH)
    find_program(UWP_SIGNTOOL signtool HINTS ${_sdk_bin_candidates} NO_DEFAULT_PATH)
    if(NOT UWP_MAKEAPPX OR NOT UWP_SIGNTOOL)
        message(FATAL_ERROR
            "makeappx.exe/signtool.exe not found in the Windows SDK bin directories. "
            "Pass -DUWP_MAKEAPPX= and -DUWP_SIGNTOOL= explicitly.")
    endif()

    if(UWP_PACKAGE_CERT_THUMBPRINT)
        set(UWP_SIGN_ARGS /fd SHA256 /sha1 ${UWP_PACKAGE_CERT_THUMBPRINT})
    elseif(UWP_PACKAGE_CERT)
        set(UWP_SIGN_ARGS /fd SHA256 /f "${UWP_PACKAGE_CERT}")
        if(UWP_PACKAGE_CERT_PASSWORD)
            list(APPEND UWP_SIGN_ARGS /p "${UWP_PACKAGE_CERT_PASSWORD}")
        endif()
    else()
        set(UWP_SIGN_ARGS "")
    endif()

    set(UWP_PACKAGE_FILE "${CMAKE_BINARY_DIR}/${PORT_NAME}.msix")

    set(UWP_EXTRACTOR_DATA_COMMANDS "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/config.yml")
        list(APPEND UWP_EXTRACTOR_DATA_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/config.yml" "${UWP_LAYOUT_DIR}/")
    endif()
    if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/ymls")
        list(APPEND UWP_EXTRACTOR_DATA_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${CMAKE_SOURCE_DIR}/assets/ymls" "${UWP_LAYOUT_DIR}/assets/ymls")
    endif()
    if(NOT UWP_EXTRACTOR_DATA_COMMANDS)
        message(STATUS "Extractor data not found; the console will need a prebuilt ac.o2r.")
    endif()

    add_custom_command(
        OUTPUT "${UWP_PACKAGE_FILE}"
        DEPENDS ${PROJECT_NAME} "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" "${UWP_MANIFEST_TEMPLATE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UWP_LAYOUT_DIR}/Assets"
        COMMAND ${CMAKE_COMMAND}
                -DUWP_COUNTER_FILE=${UWP_BUILD_COUNTER}
                -DUWP_MANIFEST_TEMPLATE=${UWP_MANIFEST_TEMPLATE}
                -DUWP_MANIFEST_OUT=${UWP_MANIFEST}
                -DUWP_PACKAGE_VERSION_BASE=${UWP_PACKAGE_VERSION_BASE}
                -DUWP_PACKAGE_NAME=${UWP_PACKAGE_NAME}
                -DUWP_PACKAGE_PUBLISHER=${UWP_PACKAGE_PUBLISHER}
                -DUWP_PACKAGE_GUID=${UWP_PACKAGE_GUID}
                -DPORT_NAME=${PORT_NAME}
                -DPROJECT_TEAM=${PROJECT_TEAM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uwp-stamp-manifest.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${UWP_LAYOUT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" "${UWP_LAYOUT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${UWP_D3DCOMPILER}" "${UWP_LAYOUT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/gamecontrollerdb.txt" "${UWP_LAYOUT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc" "${UWP_LAYOUT_DIR}/.tcc"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libtcc>" "${UWP_LAYOUT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UWP_ASSET_DIR}/Square44x44Logo.png
                ${UWP_ASSET_DIR}/Square150x150Logo.png ${UWP_ASSET_DIR}/StoreLogo.png
                ${UWP_ASSET_DIR}/SplashScreen.png "${UWP_LAYOUT_DIR}/Assets/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UWP_RUNTIME_DLLS} "${UWP_LAYOUT_DIR}/"
        ${UWP_EXTRACTOR_DATA_COMMANDS}
        COMMAND "${UWP_MAKEAPPX}" pack /o /d "${UWP_LAYOUT_DIR}" /p "${UWP_PACKAGE_FILE}"
        COMMENT "Packing ${PORT_NAME}.msix for Xbox Dev Mode"
        VERBATIM
    )

    if(UWP_SIGN_ARGS)
        add_custom_command(
            OUTPUT "${UWP_PACKAGE_FILE}.signed"
            DEPENDS "${UWP_PACKAGE_FILE}"
            COMMAND "${UWP_SIGNTOOL}" sign ${UWP_SIGN_ARGS} "${UWP_PACKAGE_FILE}"
            COMMAND ${CMAKE_COMMAND} -E touch "${UWP_PACKAGE_FILE}.signed"
            COMMENT "Signing ${PORT_NAME}.msix"
            VERBATIM
        )
        add_custom_target(bellwood_uwp_package ALL DEPENDS "${UWP_PACKAGE_FILE}.signed")
    else()
        add_custom_target(bellwood_uwp_package ALL DEPENDS "${UWP_PACKAGE_FILE}")
        message(WARNING
            "No signing certificate configured, so the package cannot be installed on a console. "
            "Pass -DUWP_PACKAGE_CERT_THUMBPRINT=<thumbprint> (subject must be ${UWP_PACKAGE_PUBLISHER}); "
            "see the Xbox section of README.md for how to create one.")
    endif()
endif()

if(IOS)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}"
            "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/${PORT_ARCHIVE}"
  )
endif()

if(ANDROID)
  FetchContent_GetProperties(libzip)
  if(libzip_SOURCE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${libzip_SOURCE_DIR}/lib ${libzip_BINARY_DIR})
  endif()
  target_link_libraries(${PROJECT_NAME} PRIVATE libzip::zip GLESv3 EGL android log)
endif()

if(EMSCRIPTEN)
  FetchContent_GetProperties(libzip)
  if(libzip_SOURCE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${libzip_SOURCE_DIR}/lib ${libzip_BINARY_DIR})
  endif()

  target_link_options(${PROJECT_NAME} PRIVATE
    -sUSE_SDL=2
    -sUSE_WEBGL2=1
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2
    -sFULL_ES3=1
    -sINITIAL_MEMORY=536870912
    -sALLOW_MEMORY_GROWTH=1
    -sEXIT_RUNTIME=1
    -sSTACK_SIZE=5242880
    -sPTHREAD_POOL_SIZE=8
    -sEMULATE_FUNCTION_POINTER_CASTS=1
    -sBINARYEN_EXTRA_PASSES=--pass-arg=max-func-params@32
    -sASYNCIFY=1
    "-sASYNCIFY_REMOVE=@${CMAKE_SOURCE_DIR}/src/port/web/asyncify-remove.txt"
    -sASYNCIFY_STACK_SIZE=8388608
    -sASSERTIONS=1
    -gsource-map
    --profiling-funcs
    "-sSOURCE_MAP_PREFIXES=${CMAKE_SOURCE_DIR}/="
    -sFORCE_FILESYSTEM=1
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString,stringToUTF8
    -lembind
    -lidbfs.js
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/src/port/web/shell.html"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/ymls@/assets/ymls"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/config.yml@/config.yml"
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}@/${PORT_ARCHIVE}"
  )

  set(BELLWOOD_WEB_PRELOAD_ARCHIVE "" CACHE FILEPATH "Path to a generated ${GAME_ARCHIVE} to embed in the web build (development only)")
  if(BELLWOOD_WEB_PRELOAD_ARCHIVE)
    target_link_options(${PROJECT_NAME} PRIVATE
      "SHELL:--preload-file ${BELLWOOD_WEB_PRELOAD_ARCHIVE}@/${GAME_ARCHIVE}")
  endif()

  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/port/web/shell.html"
    "${CMAKE_SOURCE_DIR}/src/port/web/asyncify-remove.txt"
    "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}")

  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/port/web/coi-serviceworker.js"
            "${CMAKE_SOURCE_DIR}/src/port/web/favicon.png"
            "${CMAKE_SOURCE_DIR}/src/port/web/apple-touch-icon.png"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
  )
endif()

find_program(CURL NAMES curl DOC "Path to the curl program.  Used to download files.")
execute_process(COMMAND ${CURL} -sSfL https://raw.githubusercontent.com/gabomdq/SDL_GameControllerDB/master/gamecontrollerdb.txt -o ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt OUTPUT_VARIABLE RESULT)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  install(FILES "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" DESTINATION . COMPONENT ${PROJECT_NAME})
  install(DIRECTORY "${CMAKE_BINARY_DIR}/assets/" DESTINATION ./assets COMPONENT ${PROJECT_NAME} OPTIONAL)
  install(FILES "${CMAKE_BINARY_DIR}/config.yml" DESTINATION . COMPONENT ${PROJECT_NAME} OPTIONAL)
endif()

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ./debug COMPONENT ${PROJECT_NAME})
  install(FILES "${CMAKE_BINARY_DIR}/${PORT_ARCHIVE}" DESTINATION . COMPONENT ${PROJECT_NAME})
  install(FILES "$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>" DESTINATION . COMPONENT ${PROJECT_NAME})
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    configure_file("${CMAKE_SOURCE_DIR}/bellwood.icns" "${CMAKE_BINARY_DIR}/macosx/${PORT_NAME}.icns" COPYONLY)
    configure_file("${CMAKE_SOURCE_DIR}/Info.plist" "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
INSTALL(TARGETS ${PROJECT_NAME} DESTINATION ../MacOS COMPONENT ${PROJECT_NAME})
if(ENABLE_SCRIPTING AND TARGET libtcc)
    INSTALL(FILES "$<TARGET_FILE:libtcc>" DESTINATION ../MacOS COMPONENT ${PROJECT_NAME})
endif()
INSTALL(FILES ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt DESTINATION ../MacOS COMPONENT ${PROJECT_NAME})
INSTALL(FILES ${CMAKE_BINARY_DIR}/${PORT_ARCHIVE} DESTINATION ../Resources COMPONENT ${PROJECT_NAME})
INSTALL(FILES ${CMAKE_BINARY_DIR}/config.yml DESTINATION ../Resources COMPONENT ${PROJECT_NAME} OPTIONAL)
INSTALL(DIRECTORY ${CMAKE_BINARY_DIR}/assets/ DESTINATION ../Resources/assets COMPONENT ${PROJECT_NAME} OPTIONAL)

# Fix bundle to include and relink all dependencies
install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/../MacOS/${PORT_NAME}\" \"\" \"${dirs}\")
")
# Homebrew's sdl2 is sdl2-compat, a shim that dlopens the real SDL3 at runtime;
# fixup_bundle cannot follow a dlopen, so the app aborts at launch with
# "Failed loading SDL3 library." unless SDL3 sits next to the shim in Frameworks
# (sdl2-compat checks @loader_path). No-op when building against real SDL2.
find_program(BREW_EXECUTABLE brew)
if (BREW_EXECUTABLE)
    execute_process(COMMAND ${BREW_EXECUTABLE} --prefix sdl3
                    OUTPUT_VARIABLE SDL3_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if (SDL3_PREFIX AND EXISTS "${SDL3_PREFIX}/lib/libSDL3.0.dylib")
        install(PROGRAMS "${SDL3_PREFIX}/lib/libSDL3.0.dylib"
                DESTINATION ../Frameworks RENAME libSDL3.dylib COMPONENT ${PROJECT_NAME})
    endif()
endif()
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY APPIMAGE_DESKTOP_FILE_TERMINAL YES)
set_property(TARGET ${PROJECT_NAME} PROPERTY APPIMAGE_DESKTOP_FILE "${CMAKE_SOURCE_DIR}/bellwood.desktop")
set_property(TARGET ${PROJECT_NAME} PROPERTY APPIMAGE_ICON_FILE "${CMAKE_SOURCE_DIR}/bellwood.png")

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CPACK_GENERATOR "External")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(CPACK_GENERATOR "ZIP")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(CPACK_GENERATOR "Bundle")
endif()

set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_BINARY_DIR}/configure-packaging.cmake)
configure_file(${CMAKE_SOURCE_DIR}/cmake/configure-packaging.cmake ${CPACK_PROJECT_CONFIG_FILE} @ONLY)
include(cmake/packaging.cmake)

if(NOT BELLWOOD_CROSS_BUILD)
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    find_package(libzip REQUIRED)
    add_executable(make_mod_sdk EXCLUDE_FROM_ALL tools/make_mod_sdk.cpp)
    target_compile_features(make_mod_sdk PRIVATE cxx_std_17)
    target_link_libraries(make_mod_sdk PRIVATE libzip::zip)
    add_custom_target(mod_sdk
        COMMAND make_mod_sdk ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
        DEPENDS make_mod_sdk
        VERBATIM
    )
endif()

if(XBOX_UWP)
    function(bellwood_force_dll_crt dir)
        get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_target IN LISTS _targets)
            get_target_property(_type ${_target} TYPE)
            if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
                continue()
            endif()

            set_target_properties(${_target} PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

            if(_type STREQUAL "SHARED_LIBRARY" AND UWP_STORE_LIBDIR)
                target_link_options(${_target} PRIVATE
                    /APPCONTAINER "/LIBPATH:${UWP_STORE_LIBDIR}")
                target_link_libraries(${_target} PRIVATE WindowsApp.lib)
            endif()

            get_target_property(_opts ${_target} COMPILE_OPTIONS)
            if(_opts)
                string(REPLACE "/MTd" "/MDd" _opts "${_opts}")
                string(REPLACE "-MTd" "-MDd" _opts "${_opts}")
                string(REPLACE "/MT" "/MD" _opts "${_opts}")
                string(REPLACE "-MT" "-MD" _opts "${_opts}")
                set_target_properties(${_target} PROPERTIES COMPILE_OPTIONS "${_opts}")
            endif()
        endforeach()

        get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_subdir IN LISTS _subdirs)
            bellwood_force_dll_crt("${_subdir}")
        endforeach()
    endfunction()

    bellwood_force_dll_crt("${CMAKE_SOURCE_DIR}")
endif()
