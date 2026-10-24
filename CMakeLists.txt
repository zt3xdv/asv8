cmake_minimum_required(VERSION 3.18)
project(asv8 C ASM)

set(ALPINE_BRANCH "v3.24" CACHE STRING "Alpine branch")
set(ALPINE_VERSION "3.24.2" CACHE STRING "Alpine minirootfs version")
set(ALPINE_SHA256 "9bf70a7f18ea44094cbb5f70c58f9af129c8214745743db0e68e5502cc2ce773" CACHE STRING "Alpine minirootfs sha256")

set(ROOTFS_TAR "${CMAKE_BINARY_DIR}/alpine-rootfs.tar.gz")
set(ROOTFS_DIR "${CMAKE_BINARY_DIR}/rootfs")
set(ROOTFS_LIB "${ROOTFS_DIR}/lib/ld-musl-aarch64.so.1")
set(ROOTFS_URL "https://dl-cdn.alpinelinux.org/alpine/${ALPINE_BRANCH}/releases/aarch64/alpine-minirootfs-${ALPINE_VERSION}-aarch64.tar.gz")

if(NOT EXISTS "${ROOTFS_TAR}")
    message(STATUS "downloading ${ROOTFS_URL}")
    file(DOWNLOAD "${ROOTFS_URL}" "${ROOTFS_TAR}" SHOW_PROGRESS EXPECTED_HASH SHA256=${ALPINE_SHA256} STATUS DL)
    list(GET DL 0 DL_RC)
    if(NOT DL_RC EQUAL 0)
        list(GET DL 1 DL_MSG)
        message(FATAL_ERROR "download failed: ${DL_MSG}")
    endif()
endif()

if(NOT EXISTS "${ROOTFS_LIB}")
    file(REMOVE_RECURSE "${ROOTFS_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${ROOTFS_TAR}" DESTINATION "${ROOTFS_DIR}")
endif()

file(GLOB PAYLOAD_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/payload/*")
list(REMOVE_ITEM PAYLOAD_FILES "${CMAKE_CURRENT_SOURCE_DIR}/payload/.gitkeep")

add_library(preload64 SHARED preload/dns64.c preload/shield64.s)
set_target_properties(preload64 PROPERTIES PREFIX "" OUTPUT_NAME "preload64")
target_compile_options(preload64 PRIVATE -target aarch64-linux-musl -fPIC -O2 -fno-stack-protector)
target_link_options(preload64 PRIVATE -target aarch64-linux-musl -nostdlib -Wl,-z,max-page-size=4096)

add_executable(mkbundle tools/mkbundle.c)

set(BUNDLE_BIN "${CMAKE_BINARY_DIR}/bundle.bin")
add_custom_command(
    OUTPUT "${BUNDLE_BIN}"
    COMMAND $<TARGET_FILE:mkbundle> "${BUNDLE_BIN}" $<TARGET_FILE:preload64> "${CMAKE_SOURCE_DIR}/payload" "${ROOTFS_DIR}"
    DEPENDS mkbundle preload64 ${PAYLOAD_FILES}
    COMMENT "generating bundle.bin"
)

set(BUNDLE_OBJ "${CMAKE_BINARY_DIR}/bundle.o")
add_custom_command(
    OUTPUT "${BUNDLE_OBJ}"
    COMMAND ld -m armelf -r -b binary -o bundle.o bundle.bin
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS "${BUNDLE_BIN}"
    COMMENT "embedding bundle.bin into bundle.o"
)

add_executable(asv8 src/main.c "${BUNDLE_OBJ}")
set_source_files_properties("${BUNDLE_OBJ}" PROPERTIES EXTERNAL_OBJECT true GENERATED true)
target_compile_options(asv8 PRIVATE -O2 -ffreestanding -fno-builtin -fno-stack-protector)
target_link_options(asv8 PRIVATE -static -nostdlib -Wl,-e,_start)
set_target_properties(asv8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
