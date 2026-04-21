#ifndef EFI_H
#define EFI_H

#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define IN
#define OUT
#define OPTIONAL

typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef size_t UINTN;
typedef UINT16 CHAR16;
typedef void VOID;
typedef VOID* EFI_HANDLE;
typedef UINT64 EFI_STATUS;
typedef UINT8 BOOLEAN;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;
typedef UINT64 EFI_EVENT;

typedef UINT32 EFI_MEMORY_TYPE;
typedef UINT32 EFI_ALLOCATE_TYPE;
typedef UINT32 EFI_GRAPHICS_PIXEL_FORMAT;

#define EFI_ERROR_BIT 0x8000000000000000ULL

#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR (EFI_ERROR_BIT | 1)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED (EFI_ERROR_BIT | 3)
#define EFI_BAD_BUFFER_SIZE (EFI_ERROR_BIT | 4)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5)
#define EFI_NOT_READY (EFI_ERROR_BIT | 6)
#define EFI_DEVICE_ERROR (EFI_ERROR_BIT | 7)
#define EFI_WRITE_PROTECTED (EFI_ERROR_BIT | 8)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_BIT | 9)
#define EFI_VOLUME_CORRUPTED (EFI_ERROR_BIT | 10)
#define EFI_VOLUME_FULL (EFI_ERROR_BIT | 11)
#define EFI_NO_MEDIA (EFI_ERROR_BIT | 12)
#define EFI_MEDIA_CHANGED (EFI_ERROR_BIT | 13)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14)
#define EFI_ACCESS_DENIED (EFI_ERROR_BIT | 15)
#define EFI_NO_RESPONSE (EFI_ERROR_BIT | 16)
#define EFI_NO_MAPPING (EFI_ERROR_BIT | 17)
#define EFI_TIMEOUT (EFI_ERROR_BIT | 18)
#define EFI_NOT_STARTED (EFI_ERROR_BIT | 19)
#define EFI_ALREADY_STARTED (EFI_ERROR_BIT | 20)
#define EFI_ABORTED (EFI_ERROR_BIT | 21)
#define EFI_ICMP_ERROR (EFI_ERROR_BIT | 22)
#define EFI_TFTP_ERROR (EFI_ERROR_BIT | 23)
#define EFI_PROTOCOL_ERROR (EFI_ERROR_BIT | 24)

#define AllocateAnyPages 0
#define AllocateMaxAddress 1
#define AllocateAddress 2

#define EfiReservedMemoryType 0
#define EfiLoaderCode 1
#define EfiLoaderData 2
#define EfiBootServicesCode 3
#define EfiBootServicesData 4
#define EfiRuntimeServicesCode 5
#define EfiRuntimeServicesData 6
#define EfiConventionalMemory 7
#define EfiUnusableMemory 8
#define EfiACPIReclaimMemory 9
#define EfiACPIMemoryNVS 10
#define EfiMemoryMappedIO 11
#define EfiMemoryMappedIOPortSpace 12
#define EfiPalCode 13
#define EfiPersistentMemory 14

#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define PixelRedGreenBlueReserved8BitPerColor 0
#define PixelBlueGreenRedReserved8BitPerColor 1
#define PixelBitMask 2
#define PixelBltOnly 3
#define PixelFormatMax 4

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CHAR16 *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    VOID *TestString;
    VOID *QueryMode;
    VOID *SetMode;
    VOID *SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    VOID *SetCursorPosition;
    VOID *EnableCursor;
    VOID *Mode;
};

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    OUT EFI_INPUT_KEY *Key
);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT WaitForKey;
};

typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    IN OUT UINTN *MemoryMapSize,
    OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
    OUT UINTN *MapKey,
    OUT UINTN *DescriptorSize,
    OUT UINT32 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    IN EFI_MEMORY_TYPE PoolType,
    IN UINTN Size,
    OUT VOID **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    IN EFI_ALLOCATE_TYPE Type,
    IN EFI_MEMORY_TYPE MemoryType,
    IN UINTN Pages,
    IN OUT EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    OUT VOID **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    IN EFI_GUID *Protocol,
    IN VOID *Registration,
    OUT VOID **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    IN EFI_HANDLE ImageHandle,
    IN UINTN MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(
    IN UINTN NumberOfEvents,
    IN EFI_EVENT *Event,
    OUT UINTN *Index
);

typedef struct {
    EFI_TABLE_HEADER Hdr;

    VOID *RaiseTPL;
    VOID *RestoreTPL;

    EFI_ALLOCATE_PAGES AllocatePages;
    VOID *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;

    EFI_ALLOCATE_POOL AllocatePool;
    VOID *FreePool;

    VOID *CreateEvent;
    VOID *SetTimer;
    EFI_WAIT_FOR_EVENT WaitForEvent;
    VOID *SignalEvent;
    VOID *CloseEvent;
    VOID *CheckEvent;

    VOID *InstallProtocolInterface;
    VOID *ReinstallProtocolInterface;
    VOID *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    VOID *Reserved;
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    VOID *LocateDevicePath;
    VOID *InstallConfigurationTable;

    VOID *LoadImage;
    VOID *StartImage;
    VOID *Exit;
    VOID *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    VOID *GetNextMonotonicCount;
    VOID *Stall;
    VOID *SetWatchdogTimer;

    VOID *ConnectController;
    VOID *DisconnectController;
    VOID *OpenProtocol;
    VOID *CloseProtocol;
    VOID *OpenProtocolInformation;

    VOID *ProtocolsPerHandle;
    VOID *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    VOID *InstallMultipleProtocolInterfaces;
    VOID *UninstallMultipleProtocolInterfaces;

    VOID *CalculateCrc32;
    VOID *CopyMem;
    VOID *SetMem;
    VOID *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    VOID *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

struct EFI_DEVICE_PATH_PROTOCOL {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
};

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH_PROTOCOL *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    VOID *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    IN EFI_FILE_PROTOCOL *This,
    OUT EFI_FILE_PROTOCOL **NewHandle,
    IN CHAR16 *FileName,
    IN UINT64 OpenMode,
    IN UINT64 Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    IN EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    IN EFI_FILE_PROTOCOL *This,
    IN OUT UINTN *BufferSize,
    OUT VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    IN EFI_FILE_PROTOCOL *This,
    IN EFI_GUID *InformationType,
    IN OUT UINTN *BufferSize,
    OUT VOID *Buffer
);

struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    VOID *Delete;
    EFI_FILE_READ Read;
    VOID *Write;
    VOID *GetPosition;
    VOID *SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    VOID *SetInfo;
    VOID *Flush;
    VOID *OpenEx;
    VOID *ReadEx;
    VOID *WriteEx;
    VOID *FlushEx;
};

typedef EFI_STATUS (EFIAPI *EFI_OPEN_VOLUME)(
    IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    OUT EFI_FILE_PROTOCOL **Root
);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_OPEN_VOLUME OpenVolume;
};

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT64 CreateTime[2];
    UINT64 LastAccessTime[2];
    UINT64 ModificationTime[2];
    UINT64 Attribute;
    CHAR16 FileName[260];
} EFI_FILE_INFO;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    VOID *QueryMode;
    VOID *SetMode;
    VOID *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

static EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1, 0x9562, 0x11d2,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static EFI_GUID EFI_FILE_INFO_GUID = {
    0x09576e92, 0x6d3f, 0x11d2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

#endif
