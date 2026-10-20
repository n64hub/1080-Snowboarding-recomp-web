if (MINGW)
    # The prebuilt freetype.lib targets the MSVC runtime; build FreeType from
    # source for MinGW instead.
    include(FetchContent)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype_src
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-13-3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype_src)
    if (NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(FREETYPE_FOUND TRUE)
    set(Freetype_FOUND TRUE)
    set(FREETYPE_INCLUDE_DIRS ${freetype_src_SOURCE_DIR}/include)
    set(FREETYPE_LIBRARIES freetype)
    return()
endif()

set(FREETYPE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/include)
set(FREETYPE_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/release static/vs2026/x64/freetype.lib")
add_library(Freetype::Freetype STATIC IMPORTED)
set_target_properties(Freetype::Freetype PROPERTIES
	IMPORTED_LOCATION ${FREETYPE_LIBRARIES}
)
target_include_directories(Freetype::Freetype INTERFACE
	${FREETYPE_INCLUDE_DIRS}
)
