cmake_minimum_required(VERSION 3.0)

project(efi-template)

set(INCLUDE_DIRS
        inc
        inc/gnu-efi
        inc/gnu-efi/x86_64
    )

set(SOURCE_FILES
        src/main.cc
    )
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TARGET_NAME "app")
set(OUTPUT_FILE_NAME "BOOTX64.efi")
set(DISK_NAME "disk.img")
set(TEMP_DISK_NAME "temp.img")
set(OUTPUT_DEBUG_FILE_NAME "${OUTPUT_FILE_NAME}.debug")
set(QEMU_NETWORK_INTERFACE_NAME "tap0")
set(QEMU_NETWORK_INTERFACE_MAC "00:00:00:00:00:01")

set(FILES_TO_COPY_ON_DISK
        ${CMAKE_SOURCE_DIR}/${OUTPUT_FILE_NAME}
        ${CMAKE_SOURCE_DIR}/scripts/startup.nsh
        ${CMAKE_SOURCE_DIR}/src/nyan.bin
    )

set(DISK_IMAGE "${CMAKE_BINARY_DIR}/${DISK_NAME}")
set(TEMP_IMAGE "${CMAKE_BINARY_DIR}/${TEMP_DISK_NAME}")
set(UEFI_PATH "${CMAKE_SOURCE_DIR}/bios/OVMF.fd")

set(STATIC_LIB_LIST "-l:libefi.a -l:libgnuefi.a")
set(STATIC_LIB_PATH "${CMAKE_SOURCE_DIR}/lib")
set(CRT0_PATH "${CMAKE_SOURCE_DIR}/lib/crt0-efi-x86_64.o")
set(LINK_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/elf_x86_64_efi.lds")

set(CMAKE_CXX_FLAGS " -std=c++17 -fno-stack-protector -static -D_GLIBCXX_FULLY_DYNAMIC_STRING -fpic -fshort-wchar -Wall -Wextra -mno-red-zone -DEFI_FUNCTION_WRAPPER -ggdb -O0 -s")
set(LDFLAGS "-nostdlib -znocombreloc -Bsymbolic -shared -static")
set(OBJCOPY_FLAGS -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-x86_64)
set(OBJCOPY_DEGUG_FLAGS ${OBJCOPY_FLAGS} -j .debug_info -j .debug_abbrev -j .debug_loc -j .debug_aranges -j .debug_line -j .debug_macinfo -j .debug_str)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(COMMAND which ld OUTPUT_STRIP_TRAILING_WHITESPACE  OUTPUT_VARIABLE LINKER)
execute_process(COMMAND which objcopy OUTPUT_STRIP_TRAILING_WHITESPACE  OUTPUT_VARIABLE OBJCOPY)
execute_process(COMMAND which qemu-system-x86_64 OUTPUT_STRIP_TRAILING_WHITESPACE  OUTPUT_VARIABLE QEMU)

set(SKIP_BUILD_RPATH TRUE)
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "${LINKER} ${CRT0_PATH} <OBJECTS> ${LDFLAGS} -L${STATIC_LIB_PATH} ${STATIC_LIB_LIST} -T${LINK_SCRIPT} -o<TARGET>")

include_directories(${INCLUDE_DIRS})
add_library(${TARGET_NAME} SHARED ${SOURCE_FILES})
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${OBJCOPY} ${OBJCOPY_FLAGS} $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_SOURCE_DIR}/${OUTPUT_FILE_NAME}
        COMMAND ${OBJCOPY} ${OBJCOPY_DEGUG_FLAGS} $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_SOURCE_DIR}/${OUTPUT_DEBUG_FILE_NAME}
    )

add_custom_command(OUTPUT ${DISK_IMAGE}
        COMMAND dd if=/dev/zero of=${DISK_IMAGE} bs=512 count=93750 && sudo parted ${DISK_IMAGE} -s -a minimal mklabel gpt && sudo parted ${DISK_IMAGE} -s -a minimal mkpart EFI FAT16 2048s 93716s && sudo parted ${DISK_IMAGE} -s -a minimal toggle 1 boot
    )
add_custom_target(FinalDisk ALL DEPENDS ${DISK_IMAGE})

add_custom_command(OUTPUT ${TEMP_IMAGE}
        COMMAND dd if=/dev/zero of=${TEMP_IMAGE} bs=512 count=91669
    )
add_custom_target(TemporaryDisk ALL DEPENDS ${TEMP_IMAGE})

add_custom_target(FormatTempDisk DEPENDS TemporaryDisk FinalDisk
        COMMAND mformat -i ${TEMP_IMAGE} -h 32 -t 32 -n 64 -c 1
    )

add_custom_target(CopyFilesOnDisk DEPENDS FormatTempDisk ${TARGET_NAME} )

foreach(file ${FILES_TO_COPY_ON_DISK})
    add_custom_command(TARGET CopyFilesOnDisk COMMAND mcopy -i ${TEMP_IMAGE} ${file} ::)
endforeach()

add_custom_target(OverwritePartition DEPENDS CopyFilesOnDisk
        COMMAND dd if=${TEMP_IMAGE} of=${DISK_IMAGE} bs=512 count=91669 seek=2048 conv=notrunc
    )

add_custom_target(img DEPENDS OverwritePartition)

add_custom_target(run DEPENDS img
        COMMAND ${QEMU} -s -drive file=${DISK_IMAGE} -bios ${UEFI_PATH} -soundhw pcspk -enable-kvm
        )

add_custom_target(run-net DEPENDS img
        COMMAND sudo ${QEMU} -s -drive file=${DISK_IMAGE} -bios ${UEFI_PATH}
            -net tap,ifname=${QEMU_NETWORK_INTERFACE_NAME}
            -net nic,model=e1000,macaddr=${QEMU_NETWORK_INTERFACE_MAC}
    )
