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

        void deallocate(T* ptr, std::size_t) {
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

EFI_STATUS close_event(EFI_EVENT event) {
    return uefi(bs->CloseEvent, event);
}

EFI_STATUS wait_for(EFI_EVENT event) {
    UINTN tmp;
    return uefi(bs->WaitForEvent, 1UL, &event, &tmp);
}

template<typename Interface>
EFI_STATUS handle_protocol(EFI_HANDLE handle, EFI_GUID* guid, Interface*& interface) {
    return uefi(bs->HandleProtocol, handle, guid, (void**)&interface);
}

class Handles {
    EFI_HANDLE* handles;
    UINTN size_;
    EFI_GUID guid_;
public:
    Handles(const Handles& ) = delete;
    Handles(const EFI_GUID& guid) : guid_(guid) {
        EFI_STATUS status = uefi(bs->LocateHandleBuffer, ByProtocol, &guid_, (void*)0, &size_, &handles);
        if (!EFIERR(status)) {
            size_ = 0;
            handles = nullptr;
        }
    }
    ~Handles() {
        free(handles);
    }
    
    std::size_t size() {
        return size_;
    } 

    EFI_HANDLE operator[](std::size_t n) {
        return handles[n];
    }

    template<typename Interface>
    efi::vector<Interface*> collect_interfaces() {
        efi::vector<Interface*> interfaces;
        for (std::size_t i=0; i < size_; ++i) {
            Interface* interface;
            EFI_STATUS status = handle_protocol(handles[i], &guid_, interface);
            if (status == EFI_SUCCESS) {
                interfaces.push_back(interface);
            }
        } 
        return interfaces;
    }
};

void fclose(EFI_FILE_PROTOCOL* file) {
    uefi(file->Close, file);
}

EFI_FILE_PROTOCOL* fopen(EFI_FILE_PROTOCOL* root, const wchar_t* name, std::size_t mode, std::size_t attributes) {
    EFI_FILE_PROTOCOL* file;
    EFI_STATUS status = uefi(root->Open, root, &file, (CHAR16*)name, mode, attributes);
    return !EFIERR(status) ? nullptr : file;
}

EFI_FILE_PROTOCOL* open_fs_with_file(const wchar_t* name) {
    auto handles = Handles(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID).collect_interfaces<EFI_SIMPLE_FILE_SYSTEM_PROTOCOL>();
    for (auto disk : handles) {
        EFI_FILE_PROTOCOL* root;
        EFI_STATUS status = uefi(disk->OpenVolume, disk, &root); 
        if (!EFIERR(status))
            continue;
        auto file = fopen(root, name, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
        if (!file)
            continue;
        fclose(file);
        return root;
    }
    return nullptr;
}

std::size_t fread(EFI_FILE_PROTOCOL* file, char* buffer, std::size_t n) {
    uefi(file->Read, file, &n, (void*)buffer);
    return n;
}

std::size_t fwrite(EFI_FILE_PROTOCOL* file, char* buffer, std::size_t n) {
    uefi(file->Write, file, &n, (void*)buffer);
    return n;
}

EFI_FILE_INFO* finfo(EFI_FILE_PROTOCOL* file) {
    EFI_FILE_INFO* buffer = nullptr;
    EFI_GUID guid = gEfiFileInfoGuid;
    UINTN size = 0;
    uefi(file->GetInfo, file, &guid, &size, (void*)buffer);
    buffer = (EFI_FILE_INFO*)malloc(size);
    uefi(file->GetInfo, file, &guid, &size, (void*)buffer);
    return buffer;
}

inline void bp() {
    bool wait = 1;
    while (wait);
}

efi::vector<EFI_GRAPHICS_OUTPUT_PROTOCOL*> open_screens() {
    return Handles(EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID).collect_interfaces<EFI_GRAPHICS_OUTPUT_PROTOCOL>();
}

class Screen {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* interface;
public:
    Screen(EFI_GRAPHICS_OUTPUT_PROTOCOL* interface) :interface(interface) {}
    EFI_STATUS blt(
                EFI_GRAPHICS_OUTPUT_BLT_PIXEL* buffer,
                EFI_GRAPHICS_OUTPUT_BLT_OPERATION   BltOperation,
                UINTN SourceX,
                UINTN SourceY,
                UINTN DestinationX,
                UINTN DestinationY,
                UINTN Width,
                UINTN Height,
                UINTN Delta
            ) {
        return uefi(interface->Blt, interface, buffer, BltOperation, SourceX, SourceY, DestinationX,  DestinationY, Width, Height, Delta);
    }
};

efi::vector<EFI_SERVICE_BINDING*> get_tcp4_services() {
    return Handles(EFI_TCP4_SERVICE_BINDING_PROTOCOL).collect_interfaces<EFI_SERVICE_BINDING>();
}

EFI_TCP4* socket(EFI_SERVICE_BINDING* service) {
    EFI_HANDLE handle;
    EFI_STATUS status = uefi(service->CreateChild, service, &handle);
    if (!EFIERR(status)) 
        return nullptr;
    EFI_GUID guid = EFI_TCP4_PROTOCOL;
    EFI_TCP4* tcp;
    status = handle_protocol(handle, &guid, tcp);
    if (!EFIERR(status))
        return nullptr;
    return tcp;
}

void close(EFI_SERVICE_BINDING* service, EFI_TCP4* tcp) {
    uefi(service->DestroyChild, service, (void*)tcp);
}

EFI_STATUS socket_config(EFI_TCP4* tcp, EFI_TCP4_CONFIG_DATA& config) {
    return uefi(tcp->Configure, tcp, &config);
}

EFI_STATUS connect(EFI_TCP4* tcp) {
    EFI_TCP4_CONNECTION_TOKEN token;
    EFI_STATUS status = create_event(0, 0, nullptr, nullptr, &token.CompletionToken.Event);
    if (!EFIERR(status))
        return status;
    status = uefi(tcp->Connect, tcp, &token);
    if (!EFIERR(status)) {
        close_event(token.CompletionToken.Event);
        return status;
    }
    status = wait_for(token.CompletionToken.Event);
    close_event(token.CompletionToken.Event);
    if (!EFIERR(status)) {
        return status;
    }
    return token.CompletionToken.Status;
}

EFI_STATUS cxx_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    st = SystemTable;
    bs = SystemTable->BootServices;
    
    EFI_LOADED_IMAGE* loaded_image;
    handle_protocol(ImageHandle, &LoadedImageProtocol, loaded_image);
    Print((CHAR16*)L"%X\n", loaded_image->ImageBase);
    
    auto fs = open_fs_with_file(L"startup.nsh");
    auto file = fopen(fs, L"startup.nsh", EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    char buffer[100];
    fread(file, buffer, 15);

/* bp(); */

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL* buffergpu = efi::Allocator<EFI_GRAPHICS_OUTPUT_BLT_PIXEL>().allocate(800*600);
    memset(buffergpu, 0, 800*600*sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    for (std::size_t i=0; i < 100; ++i) {
        for (std::size_t j=0; j < 100; ++j) {
            buffergpu[i*800+j].Green = 255;
        }
    }
    auto screens = open_screens();
    auto screen = Screen(screens[0]);
    screen.blt(buffergpu,EfiBltBufferToVideo,  0, 0, 0, 0, 100, 100, 800*4);
    
    auto services = get_tcp4_services();
    auto tcp = socket(services[0]);
    
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
    
    socket_config(tcp, config);
    connect(tcp);
    return EFI_SUCCESS;
}
