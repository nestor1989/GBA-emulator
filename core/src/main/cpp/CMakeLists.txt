cmake_minimum_required(VERSION 3.22.1)
project(emucore C CXX)

# Emulation must run at full speed even in debug builds: the NDK default for
# Debug is -O0, which makes mGBA miss 60 fps on low-end phones and turns QA
# results into false negatives. Debug symbols are kept.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")

# Release builds also keep debug info (-g). Gradle strips it from the libraries
# that ship in the app and, with ndk.debugSymbolLevel in app/build.gradle.kts,
# packs it into the AAB so Play Console can show readable native crash stacks.
# Same -O2 as debug, so QA tests the same optimized code that ships. Both
# Release and RelWithDebInfo are set, whichever one Gradle picks.
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(MGBA_DIR "${REPO_ROOT}/third_party/mgba")
set(OBOE_DIR "${REPO_ROOT}/third_party/oboe")

if(NOT EXISTS "${MGBA_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "mGBA submodule missing. Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# mGBA (MPL 2.0), built as a static library from the pinned submodule.
# Only what the app needs is enabled. Disabling FFmpeg and libelf also keeps
# LGPL/GPL code out of the build.
# ---------------------------------------------------------------------------
macro(mgba_option name value)
	set(${name} ${value} CACHE BOOL "" FORCE)
endmacro()

mgba_option(BUILD_QT OFF)
mgba_option(BUILD_SDL OFF)
mgba_option(BUILD_LIBRETRO OFF)
mgba_option(BUILD_HEADLESS OFF)
mgba_option(BUILD_EXAMPLE OFF)
mgba_option(BUILD_PERF OFF)
mgba_option(BUILD_TEST OFF)
mgba_option(BUILD_SUITE OFF)
mgba_option(BUILD_CINEMA OFF)
mgba_option(BUILD_PYTHON OFF)
mgba_option(BUILD_SHARED OFF)
mgba_option(BUILD_STATIC ON)
mgba_option(BUILD_LTO OFF)
mgba_option(BUILD_GL OFF)
mgba_option(BUILD_GLES2 OFF)
mgba_option(BUILD_GLES3 OFF)
mgba_option(USE_EPOXY OFF)
mgba_option(M_CORE_GBA ON)
mgba_option(M_CORE_GB ON)
# .zip through the minizip copy bundled with mGBA + system zlib; .7z through the LZMA SDK.
mgba_option(USE_ZLIB ON)
mgba_option(USE_MINIZIP OFF)
mgba_option(USE_LIBZIP OFF)
mgba_option(USE_LZMA ON)
mgba_option(USE_FFMPEG OFF)
mgba_option(USE_PNG OFF)
mgba_option(USE_SQLITE3 OFF)
mgba_option(USE_ELF OFF)
mgba_option(USE_LUA OFF)
mgba_option(USE_JSON_C OFF)
mgba_option(USE_FREETYPE OFF)
mgba_option(USE_EDITLINE OFF)
mgba_option(USE_DISCORD_RPC OFF)
mgba_option(ENABLE_SCRIPTING OFF)
mgba_option(ENABLE_DEBUGGERS OFF)
mgba_option(ENABLE_GDB_STUB OFF)

# When mGBA is added with EXCLUDE_FROM_ALL it switches itself to LIBMGBA_ONLY,
# which also sets DISABLE_DEPS and silently drops zlib, i.e. .zip support.
# Keep the full configuration (with the features above) instead.
set(LIBMGBA_ONLY OFF)
set(DISABLE_DEPS OFF CACHE BOOL "" FORCE)

add_subdirectory("${MGBA_DIR}" mgba EXCLUDE_FROM_ALL)


# Code that includes mGBA headers MUST be compiled with the same definitions as
# libmgba. The generated flags.h does not carry all of them (ENABLE_DIRECTORIES
# is missing), and a mismatch silently changes the layout of struct mCore and
# crashes at runtime. Copy both the directory-level and target-level ones.
get_directory_property(MGBA_DIR_DEFS DIRECTORY "${MGBA_DIR}" COMPILE_DEFINITIONS)
get_target_property(MGBA_TARGET_DEFS mgba COMPILE_DEFINITIONS)
if(NOT MGBA_TARGET_DEFS)
	set(MGBA_TARGET_DEFS "")
endif()
set(MGBA_ALL_DEFS ${MGBA_DIR_DEFS} ${MGBA_TARGET_DEFS})
list(REMOVE_DUPLICATES MGBA_ALL_DEFS)
if(NOT "USE_ZLIB" IN_LIST MGBA_ALL_DEFS OR NOT "USE_MINIZIP" IN_LIST MGBA_ALL_DEFS)
	message(FATAL_ERROR "mGBA was configured without zlib/minizip: .zip ROMs would not open")
endif()
set(MGBA_INCLUDE_DIRS "${MGBA_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/mgba/include")

# Platform-independent engine: emulation loop, audio ring, triple buffer, saves.
add_library(emuengine STATIC emu_core.c)
target_include_directories(emuengine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE ${MGBA_INCLUDE_DIRS})
target_compile_definitions(emuengine PRIVATE ${MGBA_ALL_DEFS})
target_link_libraries(emuengine PUBLIC mgba)
set_target_properties(emuengine PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
	if(NOT EXISTS "${OBOE_DIR}/CMakeLists.txt")
		message(FATAL_ERROR "Oboe submodule missing. Run: git submodule update --init --recursive")
	endif()
	# Oboe (Apache 2.0) from source: statically linked and built with our NDK,
	# so the 16 KB page alignment is guaranteed.
	add_subdirectory("${OBOE_DIR}" oboe EXCLUDE_FROM_ALL)
	# Oboe turns on -Werror for Debug; a new NDK warning must not break our build.
	get_target_property(OBOE_OPTIONS oboe COMPILE_OPTIONS)
	if(OBOE_OPTIONS)
		list(REMOVE_ITEM OBOE_OPTIONS "$<$<CONFIG:DEBUG>:-Werror>")
		set_target_properties(oboe PROPERTIES COMPILE_OPTIONS "${OBOE_OPTIONS}")
	endif()

	add_library(emucore SHARED
		jni_bridge.c
		video_gles.c
		audio_oboe.cpp)
	target_include_directories(emucore PRIVATE ${MGBA_INCLUDE_DIRS} "${OBOE_DIR}/include")
	target_compile_definitions(emucore PRIVATE ${MGBA_ALL_DEFS})
	target_link_libraries(emucore PRIVATE emuengine oboe GLESv2 android log z m)
	target_link_options(emucore PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")
	set_target_properties(emucore PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
else()
	# Host build (Linux/macOS) to test the engine without a device:
	#   cmake -S core/src/main/cpp -B build-host && cmake --build build-host && ./build-host/emu_host_test
	find_package(Threads REQUIRED)
	add_executable(emu_host_test "${CMAKE_CURRENT_SOURCE_DIR}/../../test/native/host_test.c")
	target_include_directories(emu_host_test PRIVATE ${MGBA_INCLUDE_DIRS})
	target_compile_definitions(emu_host_test PRIVATE ${MGBA_ALL_DEFS})
	target_link_libraries(emu_host_test PRIVATE emuengine Threads::Threads m)
	set_target_properties(emu_host_test PROPERTIES C_STANDARD 11)
endif()
