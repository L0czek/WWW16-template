extern "C" {

	#include <efi.h>
	#include <efilib.h>

}

#include <functional>
#include "pitches.h"
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
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* get_mode() {
        return interface->Mode;
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
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

EFI_STATUS sleep(std::size_t us) {
    return uefi(bs->Stall, us);
}

 static void play_sound(uint32_t nFrequence) {
 	uint32_t Div;
 	uint8_t tmp;

        //Set the PIT to the desired frequency
 	Div = 1193180 / nFrequence;
    outb(0x43, 0xb6);
 	outb(0x42, (uint8_t) (Div) );
 	outb(0x42, (uint8_t) (Div >> 8));

        //And play the sound using the PC speaker
 	tmp = inb(0x61);
  	if (tmp != (tmp | 3)) {
 		outb(0x61, tmp | 3);
 	}
 }

 //make it shutup
 static void nosound() {
 	uint8_t tmp = inb(0x61) & 0xFC;

 	outb(0x61, tmp);
 }

 //Make a beep
 void beep() {
 	 play_sound(1000);
 	 sleep(10'000);
 	 nosound();
          //set_PIT_2(old_frequency);
 }

static const char System5x7[]  = {
    0x00, 0x00, 0x00, 0x00, 0x00,// (space)
	0x00, 0x00, 0x5F, 0x00, 0x00,// !
	0x00, 0x07, 0x00, 0x07, 0x00,// "
	0x14, 0x7F, 0x14, 0x7F, 0x14,// #
	0x24, 0x2A, 0x7F, 0x2A, 0x12,// $
	0x23, 0x13, 0x08, 0x64, 0x62,// %
	0x36, 0x49, 0x55, 0x22, 0x50,// &
	0x00, 0x05, 0x03, 0x00, 0x00,// '
	0x00, 0x1C, 0x22, 0x41, 0x00,// (
	0x00, 0x41, 0x22, 0x1C, 0x00,// )
	0x08, 0x2A, 0x1C, 0x2A, 0x08,// *
	0x08, 0x08, 0x3E, 0x08, 0x08,// +
	0x00, 0x50, 0x30, 0x00, 0x00,// ,
	0x08, 0x08, 0x08, 0x08, 0x08,// -
	0x00, 0x60, 0x60, 0x00, 0x00,// .
	0x20, 0x10, 0x08, 0x04, 0x02,// /
	0x3E, 0x51, 0x49, 0x45, 0x3E,// 0
	0x00, 0x42, 0x7F, 0x40, 0x00,// 1
	0x42, 0x61, 0x51, 0x49, 0x46,// 2
	0x21, 0x41, 0x45, 0x4B, 0x31,// 3
	0x18, 0x14, 0x12, 0x7F, 0x10,// 4
	0x27, 0x45, 0x45, 0x45, 0x39,// 5
	0x3C, 0x4A, 0x49, 0x49, 0x30,// 6
	0x01, 0x71, 0x09, 0x05, 0x03,// 7
	0x36, 0x49, 0x49, 0x49, 0x36,// 8
	0x06, 0x49, 0x49, 0x29, 0x1E,// 9
	0x00, 0x36, 0x36, 0x00, 0x00,// :
	0x00, 0x56, 0x36, 0x00, 0x00,// ;
	0x00, 0x08, 0x14, 0x22, 0x41,// <
	0x14, 0x14, 0x14, 0x14, 0x14,// =
	0x41, 0x22, 0x14, 0x08, 0x00,// >
	0x02, 0x01, 0x51, 0x09, 0x06,// ?
	0x32, 0x49, 0x79, 0x41, 0x3E,// @
	0x7E, 0x11, 0x11, 0x11, 0x7E,// A
	0x7F, 0x49, 0x49, 0x49, 0x36,// B
	0x3E, 0x41, 0x41, 0x41, 0x22,// C
	0x7F, 0x41, 0x41, 0x22, 0x1C,// D
	0x7F, 0x49, 0x49, 0x49, 0x41,// E
	0x7F, 0x09, 0x09, 0x01, 0x01,// F
	0x3E, 0x41, 0x41, 0x51, 0x32,// G
	0x7F, 0x08, 0x08, 0x08, 0x7F,// H
	0x00, 0x41, 0x7F, 0x41, 0x00,// I
	0x20, 0x40, 0x41, 0x3F, 0x01,// J
	0x7F, 0x08, 0x14, 0x22, 0x41,// K
	0x7F, 0x40, 0x40, 0x40, 0x40,// L
	0x7F, 0x02, 0x04, 0x02, 0x7F,// M
	0x7F, 0x04, 0x08, 0x10, 0x7F,// N
	0x3E, 0x41, 0x41, 0x41, 0x3E,// O
	0x7F, 0x09, 0x09, 0x09, 0x06,// P
	0x3E, 0x41, 0x51, 0x21, 0x5E,// Q
	0x7F, 0x09, 0x19, 0x29, 0x46,// R
	0x46, 0x49, 0x49, 0x49, 0x31,// S
	0x01, 0x01, 0x7F, 0x01, 0x01,// T
	0x3F, 0x40, 0x40, 0x40, 0x3F,// U
	0x1F, 0x20, 0x40, 0x20, 0x1F,// V
	0x7F, 0x20, 0x18, 0x20, 0x7F,// W
	0x63, 0x14, 0x08, 0x14, 0x63,// X
	0x03, 0x04, 0x78, 0x04, 0x03,// Y
	0x61, 0x51, 0x49, 0x45, 0x43,// Z
	0x00, 0x00, 0x7F, 0x41, 0x41,// [
	0x02, 0x04, 0x08, 0x10, 0x20,// "\"
	0x41, 0x41, 0x7F, 0x00, 0x00,// ]
	0x04, 0x02, 0x01, 0x02, 0x04,// ^
	0x40, 0x40, 0x40, 0x40, 0x40,// _
	0x00, 0x01, 0x02, 0x04, 0x00,// `
	0x20, 0x54, 0x54, 0x54, 0x78,// a
	0x7F, 0x48, 0x44, 0x44, 0x38,// b
	0x38, 0x44, 0x44, 0x44, 0x20,// c
	0x38, 0x44, 0x44, 0x48, 0x7F,// d
	0x38, 0x54, 0x54, 0x54, 0x18,// e
	0x08, 0x7E, 0x09, 0x01, 0x02,// f
	0x08, 0x14, 0x54, 0x54, 0x3C,// g
	0x7F, 0x08, 0x04, 0x04, 0x78,// h
	0x00, 0x44, 0x7D, 0x40, 0x00,// i
	0x20, 0x40, 0x44, 0x3D, 0x00,// j
	0x00, 0x7F, 0x10, 0x28, 0x44,// k
	0x00, 0x41, 0x7F, 0x40, 0x00,// l
	0x7C, 0x04, 0x18, 0x04, 0x78,// m
	0x7C, 0x08, 0x04, 0x04, 0x78,// n
	0x38, 0x44, 0x44, 0x44, 0x38,// o
	0x7C, 0x14, 0x14, 0x14, 0x08,// p
	0x08, 0x14, 0x14, 0x18, 0x7C,// q
	0x7C, 0x08, 0x04, 0x04, 0x08,// r
	0x48, 0x54, 0x54, 0x54, 0x20,// s
	0x04, 0x3F, 0x44, 0x40, 0x20,// t
	0x3C, 0x40, 0x40, 0x20, 0x7C,// u
	0x1C, 0x20, 0x40, 0x20, 0x1C,// v
	0x3C, 0x40, 0x30, 0x40, 0x3C,// w
	0x44, 0x28, 0x10, 0x28, 0x44,// x
	0x0C, 0x50, 0x50, 0x50, 0x3C,// y
	0x44, 0x64, 0x54, 0x4C, 0x44,// z
	0x00, 0x08, 0x36, 0x41, 0x00,// {
	0x00, 0x00, 0x7F, 0x00, 0x00,// |
	0x00, 0x41, 0x36, 0x08, 0x00,// }
	0x08, 0x08, 0x2A, 0x1C, 0x08,// ->
	0x08, 0x1C, 0x2A, 0x08, 0x08 // <-

};

/*Nyan Cat
  with Bass
  Uses Arduino tone library pitches.h [http://arduino.cc/en/Tutorial/tone ]
       by Electric Mango (Leocarbon)
   http://electronicmango.blogspot.kr
  https://electricmango.github.io
              realleocarbon@gmail.com
  
  Thanks to Sean for posting the sheet music on
  http://junket.tumblr.com/post/4776023022/heres-the-nyan-cat-sheet-music
  
  Version 1.0.1
  -------------------------------------------------------------------------
  Nyan Cat with Bass is licensed under the
  
  Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)
  
  You are free to:
 
  Share — copy and redistribute the material in any medium or format
  Adapt — remix, transform, and build upon the material
  for any purpose, even commercially.
  The licensor cannot revoke these freedoms as long as you follow the license terms.

  Under the following terms:

  Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
  ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions under the same license as the original.

  No additional restrictions — You may not apply legal terms or technological measures that legally restrict others from doing anything the license permits.

  The full license is available at https://creativecommons.org/licenses/by-sa/4.0/legalcode
  
  Copyright (c) 2012 ~ 2014 Electric Mango (Leocarbon)
  -------------------------------------------------------------------------
  Nyan_Cat.ino
*/
 
 #include "pitches.h"

    // notes in the melody:
int melody[] = {
  NOTE_DS5, NOTE_E5, NOTE_FS5, 0, NOTE_B5, NOTE_E5, NOTE_DS5, NOTE_E5, NOTE_FS5, NOTE_B5, NOTE_DS6, NOTE_E6, NOTE_DS6, NOTE_AS5, NOTE_B5, 0,
  NOTE_FS5, 0, NOTE_DS5, NOTE_E5, NOTE_FS5, 0, NOTE_B5, NOTE_CS6, NOTE_AS5, NOTE_B5, NOTE_CS6, NOTE_E6, NOTE_DS6, NOTE_E6, NOTE_CS6,
  NOTE_FS4, NOTE_GS4, NOTE_D4, NOTE_DS4, NOTE_FS2, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_CS4, NOTE_B3,
  NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_D4, NOTE_DS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_FS4, NOTE_GS4, NOTE_D4, NOTE_DS4, NOTE_FS2, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_CS4, NOTE_B3,
  NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_D4, NOTE_DS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_B3, NOTE_B3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4,
  NOTE_B3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_E4, NOTE_DS4, NOTE_CS4, NOTE_B3, NOTE_E3, NOTE_DS3, NOTE_E3, NOTE_FS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_FS3,
  NOTE_B3, NOTE_B3, NOTE_AS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4, NOTE_B3, NOTE_AS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4,
  NOTE_B3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_E4, NOTE_DS4, NOTE_CS4, NOTE_B3, NOTE_E3, NOTE_DS3, NOTE_E3, NOTE_FS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_FS3,
  NOTE_B3, NOTE_B3, NOTE_AS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4, NOTE_B3, NOTE_CS4,
  NOTE_FS4, NOTE_GS4, NOTE_D4, NOTE_DS4, NOTE_FS2, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_CS4, NOTE_B3,
  NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_D4, NOTE_DS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_FS4, NOTE_GS4, NOTE_D4, NOTE_DS4, NOTE_FS2, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_CS4, NOTE_B3,
  NOTE_DS4, NOTE_FS4, NOTE_GS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_D4, NOTE_DS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4,
  NOTE_D4, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_FS4, NOTE_CS4, NOTE_D4, NOTE_CS4, NOTE_B3, NOTE_CS4, NOTE_B3, NOTE_B3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4,
  NOTE_B3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_E4, NOTE_DS4, NOTE_CS4, NOTE_B3, NOTE_E3, NOTE_DS3, NOTE_E3, NOTE_FS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_FS3,
  NOTE_B3, NOTE_B3, NOTE_AS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4, NOTE_B3, NOTE_AS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4,
  NOTE_B3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_E4, NOTE_DS4, NOTE_CS4, NOTE_B3, NOTE_E3, NOTE_DS3, NOTE_E3, NOTE_FS3,
  NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_B3, NOTE_CS4, NOTE_DS4, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_FS3,
  NOTE_B3, NOTE_B3, NOTE_AS3, NOTE_B3, NOTE_FS3, NOTE_GS3, NOTE_B3, NOTE_E4, NOTE_DS4, NOTE_E4, NOTE_FS4, NOTE_B3, NOTE_CS4,
};

    // note durations: 4 = quarter note, 8 = eighth note, etc.:
int noteDurations[] = {
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,8,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,8,8,8,
  8,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,8,8,8,
  8,8,16,16,16,16,16,16,8,8,8,
  8,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,8,8,8,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,16,16,8,8,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,16,16,8,8,
  8,8,16,16,16,16,16,16,8,8,8,
  8,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,8,8,8,
  8,8,16,16,16,16,16,16,8,8,8,
  8,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,8,8,8,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,16,16,8,8,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,8,16,16,16,16,16,16,16,16,16,16,16,16,
  8,16,16,8,16,16,16,16,16,16,16,16,16,16,
  8,16,16,16,16,16,16,16,16,16,16,8,8,
};

EFI_STATUS set_timer(EFI_EVENT event, EFI_TIMER_DELAY type, std::size_t time) {
    return uefi(bs->SetTimer, event, type, time);
}

EFI_EVENT gui_draw_event;
EFI_EVENT sound_event;

EFI_GRAPHICS_OUTPUT_BLT_PIXEL fb[800*600];

void fill(std::size_t w, std::size_t h, EFI_GRAPHICS_OUTPUT_BLT_PIXEL color) {
    for (std::size_t i=0; i < w; ++i) {
        for (std::size_t j=0; j < h; ++j ) {
            fb[j*800+i] = color;
        }
    }
}

void putc(char c, std::size_t x, std::size_t y) {
    for (std::size_t i=0; i < 5; ++i) {
        char column = System5x7[((int)((int)c-(int)' ')*5) + i];
        for (std::size_t j=0; j < 8; ++j) {
            if (column & (1<<j)) {
                fb[(x + j) * 800 + y + i].Red = 0xff;
                fb[(x + j) * 800 + y + i].Green = 0xff;
                fb[(x + j) * 800 + y + i].Blue = 0xff;
            }         
        }
    }    
}

void print(char* text, std::size_t x, std::size_t y) {
    while(*text) {
        putc(*text, x, y);
        y+=5;
        text++;
    }
}


struct DrawCtx {
    Screen* screen;
}draw_ctx;

void gui_draw(EFI_EVENT event, void* vctx) {
    
}

void perror(EFI_STATUS error, const wchar_t* msg) {
    switch (error) {
        case EFI_SUCCESS             : Print((CHAR16*)L"EFI_SUCCESS: %s\n", msg); break;                
        case EFI_LOAD_ERROR          : Print((CHAR16*)L"EFI_LOAD_ERROR: %s\n", msg); break;        
        case EFI_INVALID_PARAMETER   : Print((CHAR16*)L"EFI_INVALID_PARAMETER: %s\n", msg); break;        
        case EFI_UNSUPPORTED         : Print((CHAR16*)L"EFI_UNSUPPORTED: %s\n", msg); break;        
        case EFI_BAD_BUFFER_SIZE     : Print((CHAR16*)L"EFI_BAD_BUFFER_SIZE: %s\n", msg); break;        
        case EFI_BUFFER_TOO_SMALL    : Print((CHAR16*)L"EFI_BUFFER_TOO_SMALL: %s\n", msg); break;        
        case EFI_NOT_READY           : Print((CHAR16*)L"EFI_NOT_READY: %s\n", msg); break;        
        case EFI_DEVICE_ERROR        : Print((CHAR16*)L"EFI_DEVICE_ERROR: %s\n", msg); break;        
        case EFI_WRITE_PROTECTED     : Print((CHAR16*)L"EFI_WRITE_PROTECTED: %s\n", msg); break;        
        case EFI_OUT_OF_RESOURCES    : Print((CHAR16*)L"EFI_OUT_OF_RESOURCES: %s\n", msg); break;        
        case EFI_VOLUME_CORRUPTED    : Print((CHAR16*)L"EFI_VOLUME_CORRUPTED: %s\n", msg); break;        
        case EFI_VOLUME_FULL         : Print((CHAR16*)L"EFI_VOLUME_FULL: %s\n", msg); break;        
        case EFI_NO_MEDIA            : Print((CHAR16*)L"EFI_NO_MEDIA: %s\n", msg); break;        
        case EFI_MEDIA_CHANGED       : Print((CHAR16*)L"EFI_MEDIA_CHANGED: %s\n", msg); break;        
        case EFI_NOT_FOUND           : Print((CHAR16*)L"EFI_NOT_FOUND: %s\n", msg); break;        
        case EFI_ACCESS_DENIED       : Print((CHAR16*)L"EFI_ACCESS_DENIED: %s\n", msg); break;        
        case EFI_NO_RESPONSE         : Print((CHAR16*)L"EFI_NO_RESPONSE: %s\n", msg); break;        
        case EFI_NO_MAPPING          : Print((CHAR16*)L"EFI_NO_MAPPING: %s\n", msg); break;        
        case EFI_TIMEOUT             : Print((CHAR16*)L"EFI_TIMEOUT: %s\n", msg); break;        
        case EFI_NOT_STARTED         : Print((CHAR16*)L"EFI_NOT_STARTED: %s\n", msg); break;        
        case EFI_ALREADY_STARTED     : Print((CHAR16*)L"EFI_ALREADY_STARTED: %s\n", msg); break;        
        case EFI_ABORTED             : Print((CHAR16*)L"EFI_ABORTED: %s\n", msg); break;        
        case EFI_ICMP_ERROR          : Print((CHAR16*)L"EFI_ICMP_ERROR: %s\n", msg); break;        
        case EFI_TFTP_ERROR          : Print((CHAR16*)L"EFI_TFTP_ERROR: %s\n", msg); break;        
        case EFI_PROTOCOL_ERROR      : Print((CHAR16*)L"EFI_PROTOCOL_ERROR: %s\n", msg); break;        
        case EFI_INCOMPATIBLE_VERSION: Print((CHAR16*)L"EFI_INCOMPATIBLE_VERSION: %s\n", msg); break;        
        case EFI_SECURITY_VIOLATION  : Print((CHAR16*)L"EFI_SECURITY_VIOLATION: %s\n", msg); break;        
        case EFI_CRC_ERROR           : Print((CHAR16*)L"EFI_CRC_ERROR: %s\n", msg); break;        
        case EFI_END_OF_MEDIA        : Print((CHAR16*)L"EFI_END_OF_MEDIA: %s\n", msg); break;        
        case EFI_END_OF_FILE         : Print((CHAR16*)L"EFI_END_OF_FILE: %s\n", msg); break;        
        case EFI_INVALID_LANGUAGE    : Print((CHAR16*)L"EFI_INVALID_LANGUAGE: %s\n", msg); break;        
        case EFI_COMPROMISED_DATA    : Print((CHAR16*)L"EFI_COMPROMISED_DATA: %s\n", msg); break;        
        case EFI_WARN_UNKNOWN_GLYPH  : Print((CHAR16*)L"EFI_WARN_UNKNOWN_GLYPH: %s\n", msg); break;        
        case EFI_WARN_DELETE_FAILURE : Print((CHAR16*)L"EFI_WARN_DELETE_FAILURE: %s\n", msg); break;        
        case EFI_WARN_WRITE_FAILURE  : Print((CHAR16*)L"EFI_WARN_WRITE_FAILURE: %s\n", msg); break;        
        case EFI_WARN_BUFFER_TOO_SMALL: Print((CHAR16*)L"EFI_WARN_BUFFER_TOO_SMALL: %s\n", msg); break;       
        default: Print((CHAR16*)L"Unknown error: %s\n", msg); break;
    }
}

enum State {
    PlayingNote,
    PlayNoSound,
    Wait
}state = PlayingNote;

void play_note() {
    static std::size_t thisNote = 0;
    int noteDuration = 1000/noteDurations[thisNote];
    int pauseBetweenNotes = noteDuration * 1.30;
    switch (state) {
        case State::PlayingNote:     
            if (melody[thisNote]){    
                play_sound(melody[thisNote]);
                set_timer(sound_event, TimerRelative, noteDuration*10'000*2);
            } 
                state = PlayNoSound;
            break;
        case State::PlayNoSound:
            nosound();
            set_timer(sound_event, TimerRelative, noteDuration*0.3* 10'000*2);         
            state = PlayingNote;
    }
    thisNote++;
    thisNote%=1000;
}

void render_cat(char* buffer, std::size_t& frame) {
    std::size_t offset = 720 * 480 *3 * frame; 
    for (std::size_t i=0; i < 720; ++i) {
        for (std::size_t j=0; j < 480; ++j) {
           
            fb[((j + 60)*800)+i + 40].Red = buffer[offset + ((j*720) + i)*3 + 0];
            fb[((j + 60)*800)+i + 40].Green = buffer[offset + ((j*720) + i)*3 +1];
            fb[((j + 60)*800)+i + 40].Blue = buffer[offset + ((j*720) + i)*3 +2];
        }
    }
    frame += 1;
    frame %=11;
}

bool check_event(EFI_EVENT event) {
    return uefi(bs->CheckEvent, event) == EFI_SUCCESS;
}

void sound_callback(EFI_EVENT event, void* vctx) {
    play_note();
}

EFI_STATUS cxx_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    st = SystemTable;
    bs = SystemTable->BootServices;

    EFI_LOADED_IMAGE* loaded_image;
    EFI_GUID g = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    uefi(bs->HandleProtocol, ImageHandle, &LoadedImageProtocol, (void**)&loaded_image);
    Print((CHAR16*)L"%X\n", loaded_image->ImageBase);
/* bp(); */
    if (EFI_ERROR(create_event(EVT_TIMER, 0, nullptr, nullptr, &gui_draw_event))) {
        Print((CHAR16*)L"Failed to create event\n");
    }
    if (EFI_ERROR(create_event(EVT_TIMER|EVT_NOTIFY_SIGNAL, TPL_CALLBACK, sound_callback, nullptr, &sound_event))) {
        Print((CHAR16*)L"Failed to create event\n");
    }
    set_timer(gui_draw_event, TimerPeriodic, (1.0/60)*10'000'000);
    auto screens = open_screens();
    auto screen = Screen(screens[0]);
    draw_ctx.screen = &screen;
    
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* mode = screen.get_mode();
    std::size_t width = mode->Info->HorizontalResolution;
    std::size_t height = mode->Info->VerticalResolution;
    

    /* bp(); */
    char
        * ptr = (char*)malloc(12 * 1024 * 1024); 
    auto fs = open_fs_with_file(L"nyan.bin");
    auto file = fopen(fs, L"nyan.bin", EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    fread(file, ptr, 12441600);

    if (ptr == nullptr) {
        Print((CHAR16*)L"nope.\n");
    }

    /* bp(); */
        /* play_note(); */
    std::size_t frame = 0;
    /* cat(); */
    while(1) {
            
            render_cat(ptr, frame);
            print("OKIPOKI", 500, 10);
            screen.blt(fb, EfiBltBufferToVideo, 0, 0, width/2 - 400, height / 2 - 300, 800, 600, 800*4);
            sleep(50'000);
    }
    
    return EFI_SUCCESS;
}
