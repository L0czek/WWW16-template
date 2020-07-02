extern "C" {

	#include <efi.h>
	#include <efilib.h>

}

EFI_STATUS cxx_main(EFI_HANDLE, EFI_SYSTEM_TABLE*);

extern "C" {

	EFI_STATUS
	EFIAPI
	efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
        InitializeLib(ImageHandle, SystemTable);
        SystemTable->BootServices->SetWatchdogTimer(0, 0, 0, nullptr); // Disable UEFI's timer which after ~5min would reset the computer.
        return cxx_main(ImageHandle, SystemTable);
	}

}

static EFI_SYSTEM_TABLE* st;
static EFI_BOOT_SERVICES* bs;

EFI_STATUS cxx_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    st = SystemTable;
    bs = SystemTable->BootServices;
    Print((CHAR16*)L"Hello World.\n");
    return EFI_SUCCESS;
}
