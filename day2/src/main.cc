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

std::size_t send(EFI_TCP4* tcp, char* buffer, std::size_t n) {
    EFI_TCP4_IO_TOKEN token;
    EFI_STATUS status = create_event(0, 0, nullptr, nullptr, &token.CompletionToken.Event);
    if (EFI_ERROR(status))
        return 0;
    EFI_TCP4_TRANSMIT_DATA mTx;
    token.Packet.TxData = &mTx;
    mTx.Push = TRUE;
    mTx.Urgent = FALSE;
    mTx.DataLength = n;
    mTx.FragmentCount = 1;
    mTx.FragmentTable[0].FragmentLength = n;
    mTx.FragmentTable[0].FragmentBuffer = (void*)buffer;
    status = uefi(tcp->Transmit, tcp, &token);
    if (EFI_ERROR(status)) {
        close_event(token.CompletionToken.Event);
        return 0;
    }

    status = wait_for(token.CompletionToken.Event);
    close_event(token.CompletionToken.Event);
    if (EFI_ERROR(status)) {
        return 0;
    }
    return mTx.DataLength;
}

std::size_t recv(EFI_TCP4* tcp, char* buffer, std::size_t n) {
    EFI_TCP4_IO_TOKEN token;
    EFI_STATUS status = create_event(0, 0, nullptr, nullptr, &token.CompletionToken.Event);
    EFI_TCP4_RECEIVE_DATA mRx;
    token.Packet.RxData = &mRx;
    mRx.UrgentFlag = FALSE;
    mRx.DataLength = n;
    mRx.FragmentCount = 1;
    mRx.FragmentTable[0].FragmentLength = n;
    mRx.FragmentTable[0].FragmentBuffer = (void*)buffer;
    status = uefi(tcp->Receive, tcp, &token);
    if (EFI_ERROR(status)) {
        close_event(token.CompletionToken.Event);
        return 0;
    }
    status = wait_for(token.CompletionToken.Event);
    close_event(token.CompletionToken.Event);
    if (EFI_ERROR(status)) {
        return 0;
    }
    return mRx.DataLength;
}

void close(EFI_TCP4* tcp) {
    EFI_TCP4_CLOSE_TOKEN token;
    EFI_STATUS status = create_event(0, 0, nullptr, nullptr, &token.CompletionToken.Event);
    status = uefi(tcp->Close, tcp, &token);
    status = wait_for(token.CompletionToken.Event);
}

bool isKeyPressed(wchar_t ch) {
    EFI_INPUT_KEY key;
    EFI_STATUS status = uefi(st->ConIn->ReadKeyStroke, st->ConIn, &key);
    if (EFI_ERROR(status)) {
        return false;
    } else {
        return key.UnicodeChar == ch;
    }
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
    /* There's an outb %al, $imm8  encoding, for compile-time constant port numbers that fit in 8b.  (N constraint).
     * Wider immediate constants would be truncated at assemble-time (e.g. "i" constraint).
     * The  outb  %al, %dx  encoding is the only option for all other cases.
     * %1 expands to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C type */
}

#define debug(w) Print((CHAR16*)w);
EFI_STATUS cxx_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    st = SystemTable;
    bs = SystemTable->BootServices;
    

    return EFI_SUCCESS;
}
