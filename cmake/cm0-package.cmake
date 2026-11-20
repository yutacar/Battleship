#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#
# Packaging and install layout for CardputerZero Debian builds.
#
# APPLaunch layout (App Submission Guide, PLAN.md Phase 7 -- see
# NOTES.md for the full rationale and the Docker device-build runbook):
#   usr/share/APPLaunch/applications/<app>.desktop   launcher entry
#   usr/share/APPLaunch/bin/                         wrapper (Exec target) + real binary
#   usr/share/APPLaunch/share/images/<app>.png       square PNG icon
# The app's own asset lookup (src/app/asset_manager.cpp) is unrelated to the
# APPLaunch tree: it always falls back to the absolute path
# /usr/share/${APP_NAME}/{fonts,images}, so fonts/images are installed there
# too, same as before this rewrite -- only the *launcher-facing* files
# (desktop entry, executable, icon) moved under APPLaunch.
#
# Deliberately NOT installed:
#   - a systemd service (removed in this rewrite -- APPLaunch does not
#     expect a resident background service, and one is itself a review risk
#     per the guide's "background services" disclosure item)
#   - assets/audio/ and the Inter/Phosphor template fonts (assets/fonts/
#     inter-*.ttf, Phosphor-Fill.ttf): grepping src/ turns up zero
#     references to any of them -- dead weight from the upstream template,
#     never wired into any screen or sound path
#   - the LLM model (models/*.gguf, ~256MB): never installed by this file in
#     the first place. BATTLESHIP_LLM_MODEL is a runtime environment
#     variable (src/commentary/commentator_factory.cpp); an unset/missing
#     model falls back to template commentary automatically. README
#     documents manual placement for users who want it.

include(GNUInstallDirs)

set(APP_DISPLAY_NAME "Battleship" CACHE STRING "Human-readable application name used by launchers and package filename")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
set(APP_MAINTAINER "yutacar <yutacar@users.noreply.github.com>" CACHE STRING "Debian package maintainer (must match the publishing GitHub account's email -- czdev/prepublish_check.py reject a mismatch as email_mismatch). Using the GitHub-provided noreply address avoids requiring a public profile email.")
set(APP_PACKAGE_DESCRIPTION "CardputerZero Battleship game" CACHE STRING "Debian package summary")

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app-wrapper.sh.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}-launch"
    @ONLY
    FILE_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

set(APPLAUNCH_DATADIR "${CMAKE_INSTALL_DATADIR}/APPLaunch")

# ---- APPLaunch-facing files ----
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${APPLAUNCH_DATADIR}/bin"
)
install(PROGRAMS "${APP_GENERATED_DIR}/${PROJECT_NAME}-launch"
    DESTINATION "${APPLAUNCH_DATADIR}/bin"
)
install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${APPLAUNCH_DATADIR}/applications"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/battleship.png"
    DESTINATION "${APPLAUNCH_DATADIR}/share/images"
)

# ---- App's own runtime assets (AssetManager's /usr/share/<app_name> lookup
# root -- see asset_manager.cpp's add_root_if_valid calls). Only the fonts
# actually loaded at runtime (Montserrat is compiled in; NotoSans{JP,SC}
# subsets are the FreeType CJK assets) and the app icon (re-used for any
# future in-app "About" display) ship here. ----
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
    FILES_MATCHING
    PATTERN "NotoSans*.otf"
    PATTERN "LICENSE.txt"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/battleship.png"
             "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/title_logo.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "third-party-assets-license.txt"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "${APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "M5Stack")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt")

# Package= battleship (lowercase, Debian-policy-safe name), independent of
# the human-readable APP_DISPLAY_NAME used for the .desktop Name= field.
string(TOLOWER "${APP_DISPLAY_NAME}" APP_DEBIAN_PACKAGE_NAME_LOWER)
string(REGEX REPLACE "[^a-z0-9+.-]" "-" APP_DEBIAN_PACKAGE_NAME_LOWER "${APP_DEBIAN_PACKAGE_NAME_LOWER}")
# Debian revision: CardputerZero dev guideline (cardputer.cc cp0-dev) names
# the package file packagename_version-revision_arm64.deb. Bump this for a
# packaging-only rebuild of the same upstream PROJECT_VERSION.
set(APP_DEBIAN_REVISION "2" CACHE STRING "Debian package revision appended to PROJECT_VERSION")
set(CPACK_PACKAGE_FILE_NAME "${APP_DEBIAN_PACKAGE_NAME_LOWER}_${PROJECT_VERSION}-${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME_LOWER}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# SHLIBDEPS is OFF, so this list is maintained by hand against the actual
# NEEDED entries of the device binary (readelf -d battleship | grep NEEDED,
# Docker debian:trixie arm64 BATTLESHIP_LLM=OFF build -- see NOTES.md):
# libc.so.6 / libstdc++.so.6 / libgcc_s.so.1 / libfreetype.so.6 /
# libpng16.so.16 / libsystemd.so.0 (BleTransport's sd-bus, BATTLESHIP_BLE=ON
# default). fmt is linked statically, and jpeg/zlib are dropped by the
# linker's --as-needed (no symbols referenced); libgomp1 was only ever
# needed by the since-removed LLM build (llama.cpp ggml-cpu OpenMP).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc-s1, libfreetype6, libpng16-16, libsystemd0")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

include(CPack)
