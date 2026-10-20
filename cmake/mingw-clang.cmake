# Cross-compile for Windows with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-clang.cmake -DLLVM_MINGW=/path/to/llvm-mingw ...
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if (NOT LLVM_MINGW)
    set(LLVM_MINGW $ENV{LLVM_MINGW})
endif()
set(TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-clang++)
set(CMAKE_RC_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-windres)
set(CMAKE_AR ${LLVM_MINGW}/bin/llvm-ar)
set(CMAKE_RANLIB ${LLVM_MINGW}/bin/llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW}/${TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# MinGW compatibility: case-insensitive Windows header names, DirectX-Headers
# (for dxguids.h), and a prelude that works around ultra64.h redefining NULL.
set(MINGW_COMPAT "${CMAKE_CURRENT_LIST_DIR}/mingw-compat")
if (NOT DIRECTX_HEADERS)
    set(DIRECTX_HEADERS $ENV{DIRECTX_HEADERS})
endif()
set(CMAKE_C_FLAGS_INIT "-isystem ${MINGW_COMPAT}/include -isystem ${DIRECTX_HEADERS}/include/directx -isystem ${DIRECTX_HEADERS}/include/dxguids -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -include ${MINGW_COMPAT}/mingw_prelude.h")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
# Some projects name system libraries with Windows SDK capitalization.
set(MINGW_COMPAT_LIB "${CMAKE_BINARY_DIR}/mingw-compat-lib")
foreach (name Shcore DXGI D3D12 Winmm Shlwapi Ole32 Version Setupapi)
    string(TOLOWER ${name} lower)
    if (EXISTS "${LLVM_MINGW}/${TRIPLE}/lib/lib${lower}.a")
        file(MAKE_DIRECTORY "${MINGW_COMPAT_LIB}")
        file(CREATE_LINK "${LLVM_MINGW}/${TRIPLE}/lib/lib${lower}.a" "${MINGW_COMPAT_LIB}/lib${name}.a" SYMBOLIC)
    endif()
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++ -L${MINGW_COMPAT_LIB} -Wl,-Xlink=-nodefaultlib:LIBCMT -Wl,-Xlink=-nodefaultlib:OLDNAMES -Wl,-Xlink=-nodefaultlib:MSVCRT")
# Build-time tools (file_to_c, shader compilers) are Windows executables.
set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/env WINEDEBUG=-all /usr/lib/wine/wine64)
