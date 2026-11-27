#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.31)

project(battleship VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# CMake configure options

# USE_DESKTOP: whether to build desktop emulator, useful when you want preview with LVGL simulator
# For instance, '-DUSE_DESKTOP=OFF' suggest building for CM0
set(USE_DESKTOP ON CACHE BOOL "Build the desktop emulator target with LVGL SDL support")
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name used for installed asset lookup")
set(APP_ASSETS_ROOT "" CACHE PATH "Optional runtime assets root. Expected subdirectories include fonts/")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device path used by embedded builds")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
# BATTLESHIP_LLM: whether to build the llama.cpp-backed LlmCommentator
# (Phase 3b, PLAN.md §2.4). LLM commentary has since been DISCONTINUED for
# all languages (EN/JA/ZH) in favor of always-on TemplateCommentator --
# commentator_factory.cpp no longer wires up LlmCommentator even when this
# flag is ON (see its file header and NOTES.md "LLM 廃止" section). Default
# is now OFF on every platform so the llama.cpp FetchContent step never
# runs and configure/build stays fast; the flag and LlmCommentator's source
# are kept in the tree only in case local-LLM commentary is revisited later.
set(BATTLESHIP_LLM OFF CACHE BOOL "Build the llama.cpp-backed LLM commentator (discontinued; source kept for possible future revival)")
# BATTLESHIP_BLE: whether to build the BlueZ D-Bus BleTransport (Phase 6,
# PLAN.md §2.2 "BleTransport" row -- the product's default 2P connection).
# Linux-only: the implementation talks to bluetoothd over the system D-Bus
# via sd-bus (libsystemd), neither of which exists on macOS/Windows, so the
# desktop developer build on those hosts gets BATTLESHIP_BLE=0 and hides the
# BLE option in the UI ("device only"). Default: ON for any Linux target
# (CM0 device builds AND Linux desktop builds), OFF elsewhere.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BATTLESHIP_BLE_DEFAULT ON)
else()
    set(BATTLESHIP_BLE_DEFAULT OFF)
endif()
set(BATTLESHIP_BLE ${BATTLESHIP_BLE_DEFAULT} CACHE BOOL
    "Build the BlueZ D-Bus BLE transport (Linux targets only)")
if(BATTLESHIP_BLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "BATTLESHIP_BLE=ON requires a Linux target (BlueZ + libsystemd/sd-bus). "
        "Reconfigure with -DBATTLESHIP_BLE=OFF for this platform.")
endif()
# BATTLESHIP_LORA_HW: whether to build the Cap LoRa-1262 (SX1262) hardware
# transport. Hardware access is Linux-only (SPI/GPIO on the CM0), so the
# default is ON for Linux targets and OFF elsewhere (same rule as BLE).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BATTLESHIP_LORA_HW_DEFAULT ON)
else()
    set(BATTLESHIP_LORA_HW_DEFAULT OFF)
endif()
set(BATTLESHIP_LORA_HW ${BATTLESHIP_LORA_HW_DEFAULT} CACHE BOOL
    "Build the SX1262 LoRa hardware transport (Linux targets only)")
# Export compile commands
# Used for IDE IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)


include(FetchContent)


# lvgl - main ui framework
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)

# Manually download LVGL dependencies
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

# llama.cpp - Phase 3b LLM commentator backend (see BATTLESHIP_LLM option
# above). Pinned to a tagged release for reproducible builds. CPU-only:
# Metal/CUDA/Vulkan backends are disabled even on macOS so behavior stays
# close to the aarch64 CM0 target (which has no GPU backend anyway) and so
# desktop configure/build times don't balloon pulling in Metal shader
# compilation. Tests/examples/server are disabled -- only the libraries
# (llama, ggml) are needed.
if(BATTLESHIP_LLM)
    message(STATUS "Configuring BATTLESHIP_LLM: fetching llama.cpp")

    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
    set(GGML_CUDA OFF CACHE BOOL "" FORCE)
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
    set(GGML_BLAS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        llama_cpp
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG        b4487
        GIT_SHALLOW    TRUE
        # EXCLUDE_FROM_ALL keeps llama.cpp's own install(TARGETS/FILES ...)
        # rules (headers, static libs, pkg-config, CMake package config) out
        # of `cmake --install`/CPack: we only need libggml*/libllama linked
        # into our own static targets, not a redistributable llama.cpp SDK.
        # Without this, `cpack -G DEB` picked up ~3MB of ggml/llama dev
        # headers and .a files under /usr/include and /usr/lib that serve no
        # purpose in the shipped app (Phase 7 packaging review).
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(llama_cpp)
endif()

# other misc dependencies
# desktop builds
if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    # =========================
    # PNG
    # =========================
    find_package(PNG REQUIRED)

    # =========================
    # JPEG (platform abstraction handled by package manager)
    # =========================
    find_package(JPEG REQUIRED)

    if(TARGET libjpeg-turbo::libjpeg-turbo)
        set(JPEG_TARGET libjpeg-turbo::libjpeg-turbo)
    elseif(TARGET JPEG::JPEG)
        set(JPEG_TARGET JPEG::JPEG)
    else()
        message(FATAL_ERROR "No JPEG provider found")
    endif()

    # =========================
    # Freetype
    # =========================

    find_package(Freetype REQUIRED)
    # =========================
    # SDL2
    # =========================
    find_package(SDL2 CONFIG REQUIRED)

    # =========================
    # fmt
    # =========================
    find_package(fmt CONFIG REQUIRED)

    # =========================
    # ZLIB
    # =========================
    find_package(ZLIB REQUIRED)

    # =========================
    # Link safely
    # =========================
    set(LVGL_DESKTOP_DEPS
        ZLIB::ZLIB
        PNG::PNG
        ${JPEG_TARGET}
        Freetype::Freetype
        SDL2::SDL2
    )

    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1)
    
    if(MSVC)
        target_compile_options(lvgl PUBLIC /utf-8)
    endif()

# Cross builds
# This search individual dependencies from the BSP folder
# and add to the target
# this may not be the good way...
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "The cp0 toolchain should download the BSP before project() runs, "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()
    message(STATUS "Using CM0 SDK root: ${CM0_SDK_ROOT}")
    message(STATUS "Using CM0 dependency root: ${CM0_SYSROOT}")

    set(CM0_INCLUDE_HINTS
        "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/include"
        "${CM0_SYSROOT}/usr/local/include"
        "${CM0_SYSROOT}/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/include"
    )
    set(CM0_LIBRARY_HINTS
        "${CM0_SYSROOT}/usr/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/lib"
        "${CM0_SYSROOT}/usr/local/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/local/lib"
        "${CM0_SYSROOT}/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/lib"
    )

    function(cm0_find_header out_var header_name)
        find_path(${out_var}
            NAMES ${header_name}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_find_library out_var)
        find_library(${out_var}
            NAMES ${ARGN}
            HINTS ${CM0_LIBRARY_HINTS}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_add_imported_library target_name library_path include_dirs)
        if(NOT TARGET ${target_name})
            add_library(${target_name} UNKNOWN IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${library_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
            )
        endif()
    endfunction()

    # Display backend, DRM is optional. All lookups stay inside the BSP sysroot.
    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h
            "${CM0_SYSROOT}/usr/include/drm"
            "${CM0_SYSROOT}/usr/include/libdrm"
            "${CM0_SYSROOT}/usr/include"
        )
        cm0_find_library(DRM_LIBRARY drm drm.so.2)

        if(NOT DRM_XF86_INCLUDE_DIR
           OR NOT DRM_XF86_MODE_INCLUDE_DIR
           OR NOT DRM_MODE_INCLUDE_DIR
           OR NOT DRM_LIBRARY)
            message(FATAL_ERROR
                "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.\n"
                "Provide them in ${CM0_SYSROOT} or disable with -DAPP_USE_DRM=OFF."
            )
        endif()
        cm0_add_imported_library(CM0::DRM "${DRM_LIBRARY}"
            "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}"
        )
        message(STATUS "CM0 DRM display support enabled")
    endif()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h
        "${CM0_SYSROOT}/usr/include/freetype2"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FREETYPE_LIBRARY freetype)
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "Missing Freetype header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(PNG_INCLUDE_DIR png.h
        "${CM0_SYSROOT}/usr/include/libpng16"
        "${CM0_SYSROOT}/usr/include/libpng"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(PNG_LIBRARY png16 png)
    if(NOT PNG_INCLUDE_DIR OR NOT PNG_LIBRARY)
        message(FATAL_ERROR "Missing PNG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::PNG "${PNG_LIBRARY}" "${PNG_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(JPEG_LIBRARY jpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "Missing JPEG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(ZLIB_LIBRARY z)
    if(NOT ZLIB_INCLUDE_DIR OR NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "Missing ZLIB header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS
            "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
            message(STATUS "Found multiarch include: ${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FMT_LIBRARY fmt libfmt.so.10 libfmt.so.10.1.0)
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        cm0_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")
    elseif(CM0_ALLOW_FETCH_DEPS)
        message(STATUS "Missing fmt in BSP sysroot, fetching from GitHub because CM0_ALLOW_FETCH_DEPS=ON")
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG        10.1.1
            GIT_SHALLOW    TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules for app packages" FORCE)
        set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
        set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt)
            target_include_directories(fmt SYSTEM BEFORE PRIVATE ${CM0_MULTIARCH_INCLUDE_DIRS})
        endif()
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    else()
        message(FATAL_ERROR
            "Missing fmt header/library in BSP sysroot: ${CM0_SYSROOT}\n"
            "Install/copy fmt into the BSP, or reconfigure with -DCM0_ALLOW_FETCH_DEPS=ON."
        )
    endif()

    set_target_properties(CM0::PNG PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "CM0::PNG;ZLIB::ZLIB")

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)
    if(NOT PTHREAD_LIBRARY)
        message(FATAL_ERROR "Missing pthread library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    if(NOT M_LIBRARY)
        message(FATAL_ERROR "Missing math library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    set(PLATFORM_INCLUDE_DIR ${CM0_MULTIARCH_INCLUDE_DIRS})
    set(PLATFORM_LIBS ${PTHREAD_LIBRARY} ${M_LIBRARY})

    set(LVGL_INCLUDE_DIR
        ${FREETYPE_INCLUDE_DIR}
        ${PNG_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    set(LVGL_LIBS
        CM0::Freetype
        CM0::PNG
        CM0::JPEG
        ZLIB::ZLIB
    )
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIR
            ${DRM_XF86_INCLUDE_DIR}
            ${DRM_XF86_MODE_INCLUDE_DIR}
            ${DRM_MODE_INCLUDE_DIR}
        )
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    list(REMOVE_DUPLICATES PLATFORM_INCLUDE_DIR)
    list(REMOVE_DUPLICATES PLATFORM_LIBS)
    list(REMOVE_DUPLICATES LVGL_INCLUDE_DIR)
    list(REMOVE_DUPLICATES LVGL_LIBS)

    target_compile_definitions(lvgl
        PUBLIC
            USE_DESKTOP=0
            APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
    )

    # fix: issue of compiling lvgl on gcc >= 15.0
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # GCC version is available in CMAKE_C_COMPILER_VERSION
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
            target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
        endif()
    endif()

    target_include_directories(lvgl
        PUBLIC
            ${LVGL_INCLUDE_DIR}
            ${PLATFORM_INCLUDE_DIR}
    )

    target_link_libraries(lvgl
        PUBLIC
            ${LVGL_LIBS}
            ${PLATFORM_LIBS}
    )

endif()



# ====================================================
# Platform sources
# ====================================================
# Create application platform sources and static library
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
)

add_library(${PROJECT_NAME}_platform 
    STATIC
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}_platform
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
)

target_compile_definitions(${PROJECT_NAME}_platform
    PUBLIC
        APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)

target_link_libraries(${PROJECT_NAME}_platform
    PUBLIC
        lvgl
)

# ====================================================
# battleship_core sources
# - Pure C++ game logic (board, ships, shots, tracking, match state)
# - No LVGL / platform / I/O dependency, standard library only
# - Namespace: battleship::core
# ====================================================
file(GLOB_RECURSE BATTLESHIP_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
)

add_library(battleship_core STATIC
    ${BATTLESHIP_CORE_SOURCES}
)

target_include_directories(battleship_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(battleship_core PUBLIC cxx_std_17)

# ====================================================
# battleship_commentary sources (Phase 3b)
# - ICommentator abstraction, TemplateCommentator (always available),
#   snapshot->prompt summarization, and the LLM commentator when
#   BATTLESHIP_LLM=ON. No LVGL dependency: LVGL-side glue (polling timers,
#   the commentary status line subject) lives in viewmodel/, not here.
# - LlmCommentator source is only compiled in when BATTLESHIP_LLM=ON so a
#   BATTLESHIP_LLM=OFF build never needs llama.cpp headers to exist at all.
# ====================================================
set(BATTLESHIP_COMMENTARY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/commentary/snapshot_summary.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/commentary/template_commentator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/commentary/commentator_factory.cpp"
)
if(BATTLESHIP_LLM)
    list(APPEND BATTLESHIP_COMMENTARY_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/commentary/llm_commentator.cpp"
    )
endif()

add_library(battleship_commentary STATIC
    ${BATTLESHIP_COMMENTARY_SOURCES}
)

target_include_directories(battleship_commentary
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(battleship_commentary PUBLIC cxx_std_17)

target_link_libraries(battleship_commentary
    PUBLIC
        battleship_core
    PRIVATE
        logger
)

target_compile_definitions(battleship_commentary
    PUBLIC
        BATTLESHIP_LLM=$<BOOL:${BATTLESHIP_LLM}>
)

if(BATTLESHIP_LLM)
    target_link_libraries(battleship_commentary PRIVATE llama)
    find_package(Threads REQUIRED)
    target_link_libraries(battleship_commentary PRIVATE Threads::Threads)
endif()

# ====================================================
# battleship_i18n sources (Phase 5)
# - Self-rolled string catalog (StrId -> per-language literal tables) and
#   settings persistence (~/.config/battleship/settings.conf key=value).
#   Depends only on battleship_core (for ShipType in strings.h/.cpp) and the
#   standard library -- no LVGL dependency, so it stays unit-testable
#   headlessly like battleship_core/battleship_commentary/battleship_net.
# - commentary::Lang is reused as i18n::Lang (see strings.h) rather than
#   duplicating the enum, so this target also needs battleship_commentary's
#   public headers (header-only dependency: only commentator.h is included).
# - fonts.h/.cpp (CJK FreeType font loading/selection) is deliberately a
#   SEPARATE target (battleship_i18n_fonts, below, added after
#   ${PROJECT_NAME}_app_core exists) since it needs LVGL + AssetManager and
#   must not drag either into the headless battleship_core_tests binary.
# ====================================================
file(GLOB BATTLESHIP_I18N_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/i18n/*.cpp"
)
list(FILTER BATTLESHIP_I18N_SOURCES EXCLUDE REGEX ".*/fonts\\.cpp$")

add_library(battleship_i18n STATIC
    ${BATTLESHIP_I18N_SOURCES}
)

target_include_directories(battleship_i18n
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(battleship_i18n PUBLIC cxx_std_17)

target_link_libraries(battleship_i18n
    PUBLIC
        battleship_core
        battleship_commentary
)

# ====================================================
# battleship_net sources (Phase 4)
# - Wire frame codec (MAGIC/ver/type/seq/len/payload/CRC16), typed message
#   payloads, stop-and-wait reliability layer, ITransport abstraction with
#   Loopback (test, fault-injectable) + TCP (WiFi v1) implementations, UDP
#   LAN discovery, and the NetSession match state machine over PlayerView.
# - No LVGL dependency: poll-driven, clock-injected, fully unit-testable
#   headlessly. POSIX sockets only (macOS desktop + CM0 Linux alike).
# - Namespace: battleship::net
# ====================================================
file(GLOB_RECURSE BATTLESHIP_NET_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/*.cpp"
)
# src/net/ble/ is Linux-only (BlueZ over sd-bus, Phase 6): keep it out of the
# glob and add it back explicitly below, so a macOS/Windows desktop build
# never even attempts to compile it (PLAN.md Phase 6 "CMake で Linux のみに隔離").
list(FILTER BATTLESHIP_NET_SOURCES EXCLUDE REGEX "/src/net/ble/")
# src/net/lora/sx1262_radio.cpp needs Linux spidev/GPIO/i2c-dev kernel headers:
# added back below only when BATTLESHIP_LORA_HW is ON.
list(FILTER BATTLESHIP_NET_SOURCES EXCLUDE REGEX "/src/net/lora/sx1262_radio\\.cpp$")

add_library(battleship_net STATIC
    ${BATTLESHIP_NET_SOURCES}
)

target_include_directories(battleship_net
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(battleship_net PUBLIC cxx_std_17)

target_link_libraries(battleship_net
    PUBLIC
        battleship_core
)

# The BATTLESHIP_BLE macro is PUBLIC so UI layers (viewmodel/view) can gate
# BLE code paths with plain `#if BATTLESHIP_BLE` -- exactly the pattern
# battleship_commentary already uses for BATTLESHIP_LLM.
target_compile_definitions(battleship_net
    PUBLIC
        BATTLESHIP_BLE=$<BOOL:${BATTLESHIP_BLE}>
        BATTLESHIP_LORA_HW=$<BOOL:${BATTLESHIP_LORA_HW}>
)

if(BATTLESHIP_LORA_HW)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "BATTLESHIP_LORA_HW=ON requires a Linux target (spidev/GPIO/i2c-dev). "
            "Reconfigure with -DBATTLESHIP_LORA_HW=OFF for this platform.")
    endif()
    target_sources(battleship_net PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/net/lora/sx1262_radio.cpp"
    )
endif()

if(BATTLESHIP_BLE)
    target_sources(battleship_net PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ble/ble_transport.cpp"
    )
    # sd-bus lives in libsystemd. Desktop Linux: pkg-config is the normal
    # route. CM0 cross/native builds: search the BSP sysroot with the same
    # cm0_find_* helpers the other device dependencies use (pkg-config would
    # resolve against host paths there).
    if(USE_DESKTOP)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
        target_link_libraries(battleship_net PRIVATE PkgConfig::SYSTEMD)
    else()
        cm0_find_header(SYSTEMD_INCLUDE_DIR systemd/sd-bus.h ${CM0_INCLUDE_HINTS})
        cm0_find_library(SYSTEMD_LIBRARY systemd libsystemd.so libsystemd.so.0)
        if(NOT SYSTEMD_INCLUDE_DIR OR NOT SYSTEMD_LIBRARY)
            message(FATAL_ERROR
                "BATTLESHIP_BLE=ON but systemd/sd-bus.h or libsystemd was not found in the "
                "sysroot (${CM0_SYSROOT}). Install libsystemd-dev there, or configure with "
                "-DBATTLESHIP_BLE=OFF.")
        endif()
        cm0_add_imported_library(CM0::Systemd "${SYSTEMD_LIBRARY}" "${SYSTEMD_INCLUDE_DIR}")
        target_link_libraries(battleship_net PRIVATE CM0::Systemd)
    endif()
    message(STATUS "BATTLESHIP_BLE: BlueZ/sd-bus BleTransport enabled")
endif()

# ====================================================
# Model sources
# ====================================================
file(GLOB_RECURSE MODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cpp"
)

add_library(${PROJECT_NAME}_model
    STATIC
    ${MODEL_SOURCES}
)

target_include_directories(${PROJECT_NAME}_model
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
)

# ====================================================
# View sources
# ====================================================
file(GLOB_RECURSE REACTIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.cpp"
)

add_library(${PROJECT_NAME}_reactive
    STATIC
    ${REACTIVE_SOURCES}
)

target_include_directories(${PROJECT_NAME}_reactive
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_link_libraries(${PROJECT_NAME}_reactive
    PUBLIC
        lvgl
)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.cpp"  
)

add_library(${PROJECT_NAME}_view
    STATIC
    ${UI_SOURCES}
)

target_include_directories(${PROJECT_NAME}_view
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

target_link_libraries(${PROJECT_NAME}_view
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_viewmodel
        battleship_i18n_fonts
)

# ====================================================
# ViewModel sources
# ====================================================
file(GLOB_RECURSE VIEWMODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.cpp"
)

add_library(${PROJECT_NAME}_viewmodel
    STATIC
    ${VIEWMODEL_SOURCES}
)

target_include_directories(${PROJECT_NAME}_viewmodel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        ${LVGL_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}_viewmodel
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_platform
        battleship_core
        battleship_commentary
        battleship_net
        battleship_i18n
)


# ====================================================
#   Core sources
# - Application lifecycle management
# - UI screen management
# - UI asset management
# - Reactive framework based on lvgl observer mode
# - Logger based on fmt
# ====================================================
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp"
)

file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.cpp"
)

add_library(logger STATIC
    ${LOGGER_SOURCES}
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
    ${PLATFORM_INCLUDE_DIR}
)

target_link_libraries(logger
    PUBLIC
        fmt::fmt
    PRIVATE
        ${PLATFORM_LIBS}
)

# ====================================================
# battleship_i18n_fonts (Phase 5)
# - CJK FreeType font loading/selection (src/i18n/fonts.{h,cpp}). Split out
#   of battleship_i18n (above) specifically because it needs LVGL +
#   AssetManager (src/app/asset_manager.h) -- pulling those into
#   battleship_i18n would drag LVGL into battleship_core_tests, which must
#   stay headless (see tests/CMakeLists.txt; it links battleship_i18n but
#   deliberately not this target).
# ====================================================
add_library(battleship_i18n_fonts STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/i18n/fonts.cpp"
)

target_include_directories(battleship_i18n_fonts
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${LVGL_INCLUDE_DIR}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/logger"
)

target_compile_features(battleship_i18n_fonts PUBLIC cxx_std_17)

target_link_libraries(battleship_i18n_fonts
    PUBLIC
        lvgl
        battleship_i18n
    PRIVATE
        logger
)

add_library(${PROJECT_NAME}_app_core STATIC
    ${APP_SOURCES}
)

target_include_directories(${PROJECT_NAME}_app_core
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

target_compile_definitions(${PROJECT_NAME}_app_core
    PUBLIC
        APP_NAME="${APP_NAME}"
        APP_CMAKE_ASSETS_ROOT="${APP_ASSETS_ROOT}"
        APP_SOURCE_ASSETS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)

target_link_libraries(${PROJECT_NAME}_app_core
    PUBLIC
        lvgl
        logger
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
        ${PROJECT_NAME}_platform
        battleship_i18n_fonts
)

# ====================================================
# main
# ====================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_app_core
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
)

# Package deb
include(cmake/cm0-package.cmake)

# ====================================================
# Tests
# - battleship_core is LVGL/platform-free, so its test binary is only built
#   for desktop developer builds (USE_DESKTOP=ON). Device/cross builds never
#   compile or link the test target, so this cannot break CM0 builds.
# ====================================================
option(BUILD_TESTING_BATTLESHIP "Build battleship_core unit tests (GoogleTest, desktop only)" ${USE_DESKTOP})
if(USE_DESKTOP AND BUILD_TESTING_BATTLESHIP)
    enable_testing()
    add_subdirectory(tests)
endif()
