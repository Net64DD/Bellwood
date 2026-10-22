set(XBOX_UWP ON CACHE BOOL "Build the Xbox Dev Mode target (UWP app container)" FORCE)

set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT CMAKE_C_COMPILER OR NOT CMAKE_CXX_COMPILER)
    file(GLOB _bellwood_clang_candidates
        "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-cl.exe"
        "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-cl.exe"
        "$ENV{ProgramFiles}/LLVM/bin/clang-cl.exe")
    list(SORT _bellwood_clang_candidates ORDER DESCENDING)
    foreach(_candidate IN LISTS _bellwood_clang_candidates)
        if(EXISTS "${_candidate}")
            set(BELLWOOD_CLANG_CL "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT BELLWOOD_CLANG_CL)
        message(FATAL_ERROR
            "clang-cl was not found. Install the Visual Studio component "
            "'C++ Clang tools for Windows' (Microsoft.VisualStudio.ComponentGroup.ClangCL), "
            "or pass -DCMAKE_C_COMPILER/-DCMAKE_CXX_COMPILER yourself.")
    endif()

    set(CMAKE_C_COMPILER "${BELLWOOD_CLANG_CL}" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${BELLWOOD_CLANG_CL}" CACHE FILEPATH "" FORCE)
endif()

set(CMAKE_LINKER_TYPE MSVC CACHE STRING "")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
