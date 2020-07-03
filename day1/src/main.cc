extern "C" {

	#include <efi.h>
	#include <efilib.h>

}

#include <functional>
#include <vector>
#include <cstring>
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

template<typename RetVal, typename ... Args>
auto uefi(RetVal (*ptr)(Args...), Args... args) {
    typedef RetVal (*FuncPtr)(Args...);
    typedef __attribute__((ms_abi)) FuncPtr MSFuncPtr;
    MSFuncPtr mptr = reinterpret_cast<MSFuncPtr>(ptr);
    return std::invoke(mptr, args...);
}


extern "C" {
    void * malloc(std::size_t n) {
        return AllocatePool(n);
    }

    void free(void* ptr) {
        FreePool(ptr);
    }
}

namespace efi {
    template<typename T>
    struct Allocator {
        typedef T value_type;

        T* allocate(std::size_t n) {
            return reinterpret_cast<T*>(malloc(n * sizeof(T)));
        }

        void deallocate(T* ptr, std::size_t size) {
            free(ptr);
        }
    };


    template<typename T>
    using vector = std::vector<T, Allocator<T>>;
    template<typename CharT>
    using basic_string = std::basic_string<CharT, std::char_traits<CharT>, Allocator<CharT>>;
}

EFI_STATUS create_event(UINT32 type, EFI_TPL tpl, EFI_EVENT_NOTIFY func, void* ctx, EFI_EVENT* event) {
    return uefi(bs->CreateEvent, type, tpl, func, ctx, event);
}

EFI_STATUS cxx_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    st = SystemTable;
    bs = SystemTable->BootServices;
        
    EFI_LOADED_IMAGE* loaded_image;
    EFI_GUID g = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    uefi(bs->HandleProtocol, ImageHandle, &LoadedImageProtocol, (void**)&loaded_image);
    Print((CHAR16*)L"%X\n", loaded_image->ImageBase);
    bool wait = true;
    while(wait);

    EFI_GUID guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    UINTN size;
    EFI_HANDLE* handlers;
    EFI_STATUS status = uefi(bs->LocateHandleBuffer, ByProtocol, &guid, (void*)0, &size, &handlers);
    
    EFI_HANDLE handle = handlers[0];

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* interface;
    status = uefi(bs->HandleProtocol, handle, &guid, (void**)&interface);
    EFI_FILE_PROTOCOL* prot;
    status = uefi(interface->OpenVolume, interface, &prot);

    EFI_FILE_PROTOCOL* file;
    status = uefi(prot->Open, 
            prot, 
            &file, 
            (CHAR16*)L"startup.nsh", 
            (long unsigned int)EFI_FILE_MODE_READ, 
           (long unsigned int) EFI_FILE_READ_ONLY);

    char buffer[100];
    size =100;

    uefi(file->Read, file, &size, (void*)buffer);

    guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    status = uefi(bs->LocateHandleBuffer, ByProtocol, &guid, (void*)0, &size, &handlers);
    handle = handlers[0];
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gpu;
    status = uefi(bs->HandleProtocol, handle, &guid, (void**)&gpu);
    Print((CHAR16*)L" %d x %d \n", gpu->Mode->Info->HorizontalResolution, gpu->Mode->Info->VerticalResolution);

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL* buffergpu = efi::Allocator<EFI_GRAPHICS_OUTPUT_BLT_PIXEL>().allocate(800*600);
    memset(buffergpu, 0, 800*600*sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    for (std::size_t i=0; i < 100; ++i) {
        for (std::size_t j=0; j < 100; ++j) {
            buffergpu[i*800+j].Green = 255;
        }
    }
    typedef long unsigned int U;
    uefi(gpu->Blt, gpu, buffergpu, EfiBltBufferToVideo, (U)0,(U) 0, (U)0, (U)0, (U)100, (U)100,(U) 800*sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));


    guid = EFI_TCP4_SERVICE_BINDING_PROTOCOL;
    status = uefi(bs->LocateHandleBuffer, ByProtocol, &guid, (void*)0, &size, &handlers);
    handle = handlers[0];
    EFI_SERVICE_BINDING* pnet;
    status = uefi(bs->HandleProtocol, handle, &guid, (void**)&pnet);
    EFI_HANDLE tcp_handle;
    status = uefi(pnet->CreateChild, pnet, &tcp_handle);
    
    EFI_TCP4* tcp;
    guid = EFI_TCP4_PROTOCOL;
    status = uefi(bs->HandleProtocol, tcp_handle, &guid, (void**)&tcp);

    EFI_TCP4_CONFIG_DATA config {
        0,
        255,
        {
            TRUE,
            { {0, 0, 0, 0} },
            { {0, 0, 0, 0} },
            1337,
            { {192, 168, 100, 1 } },
            4444,
            TRUE
        },
        NULL
    };


    status = uefi(tcp->Configure, tcp, &config);

    EFI_TCP4_CONNECTION_TOKEN token;
    
    status = create_event(0, 0, NULL, NULL, &token.CompletionToken.Event);

    status = uefi(tcp->Connect, tcp, &token);

    UINTN tmp;
    
    status = uefi(bs->WaitForEvent, 1UL, &token.CompletionToken.Event, &tmp);


    return EFI_SUCCESS;
}
