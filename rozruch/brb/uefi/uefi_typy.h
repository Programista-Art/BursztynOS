/*
 * Bursztynowy Program Rozruchowy (BRB) - minimalna warstwa deklaracji UEFI.
 *
 * Napisane samodzielnie na podstawie:
 *   Unified Extensible Firmware Interface (UEFI) Specification, Release 2.10 (UEFI Forum, 2022):
 *     2.3.1  Data Types (typy podstawowe, EFI_GUID, EFI_STATUS),
 *     2.3.4  x64 Platforms (konwencja wywolan Microsoft x64),
 *     4.2    EFI Table Header,
 *     4.3    EFI System Table,
 *     4.4    EFI Boot Services Table,
 *     7.1    Event, Timer, and Task Priority Services,
 *     7.2    Memory Allocation Services (AllocatePages/FreePages - Faza 3),
 *     8.3    Time Services (EFI_TIME w EFI_FILE_INFO - Faza 3),
 *     9.1    EFI Loaded Image Protocol,
 *     12.3   Simple Text Input Protocol,
 *     12.4   Simple Text Output Protocol,
 *     13.4   Simple File System Protocol (Faza 3),
 *     13.5   File Protocol, EFI_FILE_INFO (Faza 3),
 *     Appendix D  Status Codes.
 * Zaden fragment nie pochodzi z EDK II, gnu-efi ani innych programow rozruchowych.
 *
 * Zakres: wylacznie to, czego potrzebuje Faza 2. Struktury tablic firmware sa jednak kompletne
 * (wszystkie pola posrednie), bo przesuniecia pozniejszych wskaznikow funkcji sa czescia ABI.
 * Pola, ktorych BRB nie wywoluje, maja typ EFI_NIEUZYWANE (wskaznik o rozmiarze UINTN).
 *
 * Wszystkie wskazniki funkcji firmware maja atrybut EFIAPI = __attribute__((ms_abi)). Reszta BRB jest
 * kompilowana w konwencji System V. GCC sam tworzy 32-bajtowy obszar "shadow space", zachowuje
 * RSI/RDI (nieulotne w ms_abi, ulotne w SysV) i utrzymuje wyrownanie stosu do 16 B na kazdej granicy
 * ms_abi <-> SysV. Kod jest kompilowany z -mgeneral-regs-only, wiec nie uzywa rejestrow XMM
 * (XMM6-XMM15 sa nieulotne w ms_abi i nie musza byc zapisywane).
 */
#ifndef BRB_UEFI_TYPY_H
#define BRB_UEFI_TYPY_H

#include <stddef.h>
#include <stdint.h>

#if !defined(__x86_64__)
#error "BRB obsluguje wylacznie architekture x86-64."
#endif

#define EFIAPI __attribute__((ms_abi))

/* ---- 2.3.1 Typy podstawowe ---- */
typedef uint8_t   BOOLEAN;
typedef int8_t    INT8;
typedef uint8_t   UINT8;
typedef int16_t   INT16;
typedef uint16_t  UINT16;
typedef int32_t   INT32;
typedef uint32_t  UINT32;
typedef int64_t   INT64;
typedef uint64_t  UINT64;
typedef int64_t   INTN;
typedef uint64_t  UINTN;
typedef char16_t  CHAR16;   /* UCS-2, 16 bitow (tak samo jak uint16_t) */
typedef void      VOID;
typedef UINTN     EFI_STATUS;
typedef VOID*     EFI_HANDLE;
typedef VOID*     EFI_EVENT;
typedef UINTN     EFI_TPL;
typedef VOID*     EFI_NIEUZYWANE;   /* pole tablicy firmware, ktorego BRB nie wywoluje */

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* ---- Appendix D: kody statusu ---- */
#define EFI_BLAD_BIT                 0x8000000000000000ULL
#define EFI_BLAD(kod)                (EFI_BLAD_BIT | (UINT64)(kod))
#define EFI_ERROR(s)                 (((UINT64)(s) & EFI_BLAD_BIT) != 0)

#define EFI_SUCCESS                  0ULL
#define EFI_LOAD_ERROR               EFI_BLAD(1)
#define EFI_INVALID_PARAMETER        EFI_BLAD(2)
#define EFI_UNSUPPORTED              EFI_BLAD(3)
#define EFI_BAD_BUFFER_SIZE          EFI_BLAD(4)
#define EFI_BUFFER_TOO_SMALL         EFI_BLAD(5)
#define EFI_NOT_READY                EFI_BLAD(6)
#define EFI_DEVICE_ERROR             EFI_BLAD(7)
#define EFI_OUT_OF_RESOURCES         EFI_BLAD(9)
#define EFI_NOT_FOUND                EFI_BLAD(14)
#define EFI_TIMEOUT                  EFI_BLAD(18)
#define EFI_ABORTED                  EFI_BLAD(21)
#define EFI_INCOMPATIBLE_VERSION     EFI_BLAD(25)
#define EFI_WARN_UNKNOWN_GLYPH       1ULL

/* ---- 4.2 Naglowek tablicy ---- */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

#define EFI_SYSTEM_TABLE_SIGNATURE   0x5453595320494249ULL  /* "IBI SYST" */
#define EFI_BOOT_SERVICES_SIGNATURE  0x56524553544F4F42ULL  /* "BOOTSERV" */
#define EFI_REWIZJA(glowna, poboczna) ((UINT32)(((glowna) << 16) | (poboczna)))

/* ---- 12.3 Simple Text Input Protocol ---- */
typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, EFI_INPUT_KEY* Key);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET    Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT          WaitForKey;
};

#define EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID \
    { 0x387477C1u, 0x69C7u, 0x11D2u, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/* ---- 12.4 Simple Text Output Protocol ---- */
typedef struct {
    INT32   MaxMode;
    INT32   Mode;
    INT32   Attribute;
    INT32   CursorColumn;
    INT32   CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_STOP;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_STOP* This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_STOP* This, const CHAR16* String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(EFI_STOP* This, const CHAR16* String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(EFI_STOP* This, UINTN ModeNumber, UINTN* Columns, UINTN* Rows);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(EFI_STOP* This, UINTN ModeNumber);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(EFI_STOP* This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(EFI_STOP* This);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(EFI_STOP* This, UINTN Column, UINTN Row);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(EFI_STOP* This, BOOLEAN Visible);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET               Reset;
    EFI_TEXT_STRING              OutputString;
    EFI_TEXT_TEST_STRING         TestString;
    EFI_TEXT_QUERY_MODE          QueryMode;
    EFI_TEXT_SET_MODE            SetMode;
    EFI_TEXT_SET_ATTRIBUTE       SetAttribute;
    EFI_TEXT_CLEAR_SCREEN        ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR       EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE*     Mode;
};

#define EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID \
    { 0x387477C2u, 0x69C7u, 0x11D2u, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/* Atrybuty tekstu (12.4, SetAttribute): EFI_TEXT_ATTR(przod, tlo) = przod | (tlo << 4). */
#define EFI_LIGHTGRAY  0x07u
#define EFI_BROWN      0x06u
#define EFI_YELLOW     0x0Eu
#define EFI_WHITE      0x0Fu
#define EFI_BLACK      0x00u
#define EFI_TEXT_ATTR(przod, tlo) ((UINTN)((przod) | ((tlo) << 4)))

/* ---- 7.1 Zdarzenia i zegary ---- */
#define EVT_TIMER        0x80000000u
#define TPL_APPLICATION  4u
#define TPL_CALLBACK     8u

typedef enum { TimerCancel = 0, TimerPeriodic = 1, TimerRelative = 2 } EFI_TIMER_DELAY;

typedef VOID (EFIAPI *EFI_EVENT_NOTIFY)(EFI_EVENT Event, VOID* Context);
typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT)(UINT32 Type, EFI_TPL NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction,
                                              VOID* NotifyContext, EFI_EVENT* Event);
typedef EFI_STATUS (EFIAPI *EFI_SET_TIMER)(EFI_EVENT Event, EFI_TIMER_DELAY Type, UINT64 TriggerTime);
typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(UINTN NumberOfEvents, EFI_EVENT* Event, UINTN* Index);
typedef EFI_STATUS (EFIAPI *EFI_CLOSE_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, const EFI_GUID* Protocol, VOID** Interface);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN Microseconds);

/* ---- 7.2 Memory Allocation Services (Faza 3: bufor pliku jadra) ---- */
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef enum { AllocateAnyPages = 0, AllocateMaxAddress = 1, AllocateAddress = 2 } EFI_ALLOCATE_TYPE;
typedef enum {
    EfiReservedMemoryType = 0, EfiLoaderCode = 1, EfiLoaderData = 2, EfiBootServicesCode = 3,
    EfiBootServicesData = 4, EfiRuntimeServicesCode = 5, EfiRuntimeServicesData = 6, EfiConventionalMemory = 7,
    EfiUnusableMemory = 8, EfiACPIReclaimMemory = 9, EfiACPIMemoryNVS = 10, EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12, EfiPalCode = 13, EfiPersistentMemory = 14, EfiUnacceptedMemoryType = 15,
    EfiMaxMemoryType = 16
} EFI_MEMORY_TYPE;
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages,
                                                EFI_PHYSICAL_ADDRESS* Memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);

/* Deskryptor mapy pamieci (7.2, GetMemoryMap). UWAGA: firmware zwraca DescriptorSize, ktory moze byc WIEKSZY
 * niz sizeof(EFI_MEMORY_DESCRIPTOR) - kolejne deskryptory czyta sie z krokiem DescriptorSize, nigdy z krokiem
 * rozmiaru tej struktury. BRB czyta pola bajtowo (wspolne/mapa_pamieci.cpp), struktura sluzy do asercji ukladu. */
#define EFI_MEMORY_DESCRIPTOR_VERSION 1u
typedef struct {
    UINT32               Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    UINT64               VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Atrybuty pamieci (7.2). */
#define EFI_MEMORY_UC            0x0000000000000001ull
#define EFI_MEMORY_WC            0x0000000000000002ull
#define EFI_MEMORY_WT            0x0000000000000004ull
#define EFI_MEMORY_WB            0x0000000000000008ull
#define EFI_MEMORY_UCE           0x0000000000000010ull
#define EFI_MEMORY_WP            0x0000000000001000ull
#define EFI_MEMORY_RP            0x0000000000002000ull
#define EFI_MEMORY_XP            0x0000000000004000ull
#define EFI_MEMORY_NV            0x0000000000008000ull
#define EFI_MEMORY_MORE_RELIABLE 0x0000000000010000ull
#define EFI_MEMORY_RO            0x0000000000020000ull
#define EFI_MEMORY_SP            0x0000000000040000ull
#define EFI_MEMORY_CPU_CRYPTO    0x0000000000080000ull
#define EFI_MEMORY_RUNTIME       0x8000000000000000ull

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN* MemoryMapSize, EFI_MEMORY_DESCRIPTOR* MemoryMap, UINTN* MapKey,
                                                UINTN* DescriptorSize, UINT32* DescriptorVersion);

/* ---- 4.4 EFI Boot Services Table: KOMPLETNY uklad 44 wskaznikow ---- */
typedef struct {
    EFI_TABLE_HEADER    Hdr;
    /* Task Priority Services */
    EFI_NIEUZYWANE      RaiseTPL;
    EFI_NIEUZYWANE      RestoreTPL;
    /* Memory Services */
    EFI_ALLOCATE_PAGES  AllocatePages;
    EFI_FREE_PAGES      FreePages;
    EFI_GET_MEMORY_MAP  GetMemoryMap;
    EFI_NIEUZYWANE      AllocatePool;
    EFI_NIEUZYWANE      FreePool;
    /* Event & Timer Services */
    EFI_CREATE_EVENT    CreateEvent;
    EFI_SET_TIMER       SetTimer;
    EFI_WAIT_FOR_EVENT  WaitForEvent;
    EFI_NIEUZYWANE      SignalEvent;
    EFI_CLOSE_EVENT     CloseEvent;
    EFI_NIEUZYWANE      CheckEvent;
    /* Protocol Handler Services */
    EFI_NIEUZYWANE      InstallProtocolInterface;
    EFI_NIEUZYWANE      ReinstallProtocolInterface;
    EFI_NIEUZYWANE      UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    VOID*               Reserved;
    EFI_NIEUZYWANE      RegisterProtocolNotify;
    EFI_NIEUZYWANE      LocateHandle;
    EFI_NIEUZYWANE      LocateDevicePath;
    EFI_NIEUZYWANE      InstallConfigurationTable;
    /* Image Services */
    EFI_NIEUZYWANE      LoadImage;
    EFI_NIEUZYWANE      StartImage;
    EFI_NIEUZYWANE      Exit;
    EFI_NIEUZYWANE      UnloadImage;
    EFI_NIEUZYWANE      ExitBootServices;
    /* Miscellaneous Services */
    EFI_NIEUZYWANE      GetNextMonotonicCount;
    EFI_STALL           Stall;
    EFI_NIEUZYWANE      SetWatchdogTimer;
    /* DriverSupport Services */
    EFI_NIEUZYWANE      ConnectController;
    EFI_NIEUZYWANE      DisconnectController;
    /* Open and Close Protocol Services */
    EFI_NIEUZYWANE      OpenProtocol;
    EFI_NIEUZYWANE      CloseProtocol;
    EFI_NIEUZYWANE      OpenProtocolInformation;
    /* Library Services */
    EFI_NIEUZYWANE      ProtocolsPerHandle;
    EFI_NIEUZYWANE      LocateHandleBuffer;
    EFI_NIEUZYWANE      LocateProtocol;
    EFI_NIEUZYWANE      InstallMultipleProtocolInterfaces;
    EFI_NIEUZYWANE      UninstallMultipleProtocolInterfaces;
    /* 32-bit CRC Services */
    EFI_NIEUZYWANE      CalculateCrc32;
    /* Miscellaneous Services */
    EFI_NIEUZYWANE      CopyMem;
    EFI_NIEUZYWANE      SetMem;
    EFI_NIEUZYWANE      CreateEventEx;
} EFI_BOOT_SERVICES;

/* ---- 4.3 EFI System Table ---- */
typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16*                          FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL*  ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    VOID*                            RuntimeServices;      /* nieuzywane w Fazie 2 */
    EFI_BOOT_SERVICES*               BootServices;
    UINTN                            NumberOfTableEntries;
    VOID*                            ConfigurationTable;   /* nieuzywane w Fazie 2 */
} EFI_SYSTEM_TABLE;

/* ---- 9.1 EFI Loaded Image Protocol ---- */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1u, 0x9562u, 0x11D2u, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_LOADED_IMAGE_PROTOCOL_REVISION 0x1000u

typedef struct {
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE        DeviceHandle;
    VOID*             FilePath;          /* EFI_DEVICE_PATH_PROTOCOL* - nieuzywane w Fazie 2 */
    VOID*             Reserved;
    UINT32            LoadOptionsSize;
    VOID*             LoadOptions;
    VOID*             ImageBase;
    UINT64            ImageSize;
    UINT32            ImageCodeType;     /* EFI_MEMORY_TYPE */
    UINT32            ImageDataType;     /* EFI_MEMORY_TYPE */
    EFI_NIEUZYWANE    Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ---- 13.4 Simple File System Protocol ---- */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964E5B22u, 0x6459u, 0x11D2u, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000ull

struct EFI_FILE_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This,
                                                                          EFI_FILE_PROTOCOL** Root);
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                                      Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};

/* ---- 13.5 File Protocol (rewizja 2: pola OpenEx..FlushEx istnieja tylko przy Revision >= 0x20000) ---- */
#define EFI_FILE_PROTOCOL_REVISION   0x00010000ull
#define EFI_FILE_PROTOCOL_REVISION2  0x00020000ull
#define EFI_FILE_MODE_READ           0x0000000000000001ull
#define EFI_FILE_DIRECTORY           0x0000000000000010ull

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL* This, EFI_FILE_PROTOCOL** NewHandle,
                                           const CHAR16* FileName, UINT64 OpenMode, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL* This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL* This, UINTN* BufferSize, VOID* Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL* This, const EFI_GUID* InformationType,
                                               UINTN* BufferSize, VOID* Buffer);

struct EFI_FILE_PROTOCOL {
    UINT64            Revision;
    EFI_FILE_OPEN     Open;
    EFI_FILE_CLOSE    Close;
    EFI_NIEUZYWANE    Delete;
    EFI_FILE_READ     Read;
    EFI_NIEUZYWANE    Write;          /* BRB nigdy nie pisze na wolumin rozruchowy */
    EFI_NIEUZYWANE    GetPosition;
    EFI_NIEUZYWANE    SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    EFI_NIEUZYWANE    SetInfo;
    EFI_NIEUZYWANE    Flush;
    EFI_NIEUZYWANE    OpenEx;         /* rewizja 2 */
    EFI_NIEUZYWANE    ReadEx;
    EFI_NIEUZYWANE    WriteEx;
    EFI_NIEUZYWANE    FlushEx;
};

/* EFI_TIME (8.3) i EFI_FILE_INFO (13.5.16). FileName ma zmienna dlugosc (koniec struktury). */
typedef struct {
    UINT16 Year;
    UINT8  Month, Day, Hour, Minute, Second, Pad1;
    UINT32 Nanosecond;
    INT16  TimeZone;
    UINT8  Daylight, Pad2;
} EFI_TIME;

#define EFI_FILE_INFO_ID \
    { 0x09576E92u, 0x6D3Fu, 0x11D2u, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_FILE_INFO_STALA_CZESC 80u   /* SIZE_OF_EFI_FILE_INFO: pola przed FileName */

typedef struct {
    UINT64   Size;
    UINT64   FileSize;
    UINT64   PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64   Attribute;
    CHAR16   FileName[1];
} EFI_FILE_INFO;

/* ---- Asercje ukladu (wartosci wyliczone recznie ze specyfikacji, x64: UINTN i wskazniki = 8 B) ---- */
static_assert(sizeof(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL) == 16, "SIMPLE_FILE_SYSTEM");
static_assert(offsetof(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL, OpenVolume) == 8, "SFS.OpenVolume");
static_assert(sizeof(EFI_FILE_PROTOCOL) == 0x78, "EFI_FILE_PROTOCOL (rewizja 2)");
static_assert(offsetof(EFI_FILE_PROTOCOL, Open) == 0x08, "FILE.Open");
static_assert(offsetof(EFI_FILE_PROTOCOL, Close) == 0x10, "FILE.Close");
static_assert(offsetof(EFI_FILE_PROTOCOL, Read) == 0x20, "FILE.Read");
static_assert(offsetof(EFI_FILE_PROTOCOL, Write) == 0x28, "FILE.Write");
static_assert(offsetof(EFI_FILE_PROTOCOL, GetInfo) == 0x40, "FILE.GetInfo");
static_assert(offsetof(EFI_FILE_PROTOCOL, Flush) == 0x50, "FILE.Flush (koniec rewizji 1)");
static_assert(offsetof(EFI_FILE_PROTOCOL, OpenEx) == 0x58, "FILE.OpenEx");
static_assert(sizeof(EFI_TIME) == 16, "EFI_TIME");
static_assert(offsetof(EFI_TIME, Nanosecond) == 8 && offsetof(EFI_TIME, TimeZone) == 12, "EFI_TIME pola");
static_assert(offsetof(EFI_FILE_INFO, FileSize) == 8, "FILE_INFO.FileSize");
static_assert(offsetof(EFI_FILE_INFO, PhysicalSize) == 16, "FILE_INFO.PhysicalSize");
static_assert(offsetof(EFI_FILE_INFO, CreateTime) == 24, "FILE_INFO.CreateTime");
static_assert(offsetof(EFI_FILE_INFO, ModificationTime) == 56, "FILE_INFO.ModificationTime");
static_assert(offsetof(EFI_FILE_INFO, Attribute) == 72, "FILE_INFO.Attribute");
static_assert(offsetof(EFI_FILE_INFO, FileName) == EFI_FILE_INFO_STALA_CZESC, "FILE_INFO.FileName");
static_assert(offsetof(EFI_BOOT_SERVICES, FreePages) == 0x30, "BS.FreePages");
static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap) == 0x38, "BS.GetMemoryMap");
static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40, "EFI_MEMORY_DESCRIPTOR (bez dopelnienia firmware)");
static_assert(offsetof(EFI_MEMORY_DESCRIPTOR, PhysicalStart) == 8, "MD.PhysicalStart (4 B wypelnienia po Type)");
static_assert(offsetof(EFI_MEMORY_DESCRIPTOR, VirtualStart) == 16, "MD.VirtualStart");
static_assert(offsetof(EFI_MEMORY_DESCRIPTOR, NumberOfPages) == 24, "MD.NumberOfPages");
static_assert(offsetof(EFI_MEMORY_DESCRIPTOR, Attribute) == 32, "MD.Attribute");
static_assert(sizeof(EFI_ALLOCATE_TYPE) == 4 && sizeof(EFI_MEMORY_TYPE) == 4, "enum UEFI = 32 bity");
static_assert(sizeof(UINTN) == 8 && sizeof(VOID*) == 8, "x64: UINTN i wskaznik maja 8 B");
static_assert(sizeof(CHAR16) == 2 && alignof(CHAR16) == 2, "CHAR16 = 16 bitow");
static_assert(sizeof(BOOLEAN) == 1, "BOOLEAN = 1 B");
static_assert(sizeof(EFI_GUID) == 16 && alignof(EFI_GUID) == 4, "EFI_GUID: 16 B, wyrownanie 32 bity");
static_assert(sizeof(EFI_TABLE_HEADER) == 24, "EFI_TABLE_HEADER");
static_assert(offsetof(EFI_TABLE_HEADER, HeaderSize) == 12, "EFI_TABLE_HEADER.HeaderSize");

static_assert(sizeof(EFI_SYSTEM_TABLE) == 120, "EFI_SYSTEM_TABLE");
static_assert(offsetof(EFI_SYSTEM_TABLE, FirmwareVendor) == 0x18, "ST.FirmwareVendor");
static_assert(offsetof(EFI_SYSTEM_TABLE, FirmwareRevision) == 0x20, "ST.FirmwareRevision");
static_assert(offsetof(EFI_SYSTEM_TABLE, ConsoleInHandle) == 0x28, "ST.ConsoleInHandle");
static_assert(offsetof(EFI_SYSTEM_TABLE, ConIn) == 0x30, "ST.ConIn");
static_assert(offsetof(EFI_SYSTEM_TABLE, ConOut) == 0x40, "ST.ConOut");
static_assert(offsetof(EFI_SYSTEM_TABLE, StdErr) == 0x50, "ST.StdErr");
static_assert(offsetof(EFI_SYSTEM_TABLE, RuntimeServices) == 0x58, "ST.RuntimeServices");
static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 0x60, "ST.BootServices");
static_assert(offsetof(EFI_SYSTEM_TABLE, NumberOfTableEntries) == 0x68, "ST.NumberOfTableEntries");
static_assert(offsetof(EFI_SYSTEM_TABLE, ConfigurationTable) == 0x70, "ST.ConfigurationTable");

static_assert(sizeof(EFI_BOOT_SERVICES) == 24 + 44 * 8, "EFI_BOOT_SERVICES: naglowek + 44 wskazniki");
static_assert(offsetof(EFI_BOOT_SERVICES, RaiseTPL) == 0x18, "BS.RaiseTPL");
static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePages) == 0x28, "BS.AllocatePages");
static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap) == 0x38, "BS.GetMemoryMap");
static_assert(offsetof(EFI_BOOT_SERVICES, CreateEvent) == 0x50, "BS.CreateEvent");
static_assert(offsetof(EFI_BOOT_SERVICES, SetTimer) == 0x58, "BS.SetTimer");
static_assert(offsetof(EFI_BOOT_SERVICES, WaitForEvent) == 0x60, "BS.WaitForEvent");
static_assert(offsetof(EFI_BOOT_SERVICES, CloseEvent) == 0x70, "BS.CloseEvent");
static_assert(offsetof(EFI_BOOT_SERVICES, HandleProtocol) == 0x98, "BS.HandleProtocol");
static_assert(offsetof(EFI_BOOT_SERVICES, Reserved) == 0xA0, "BS.Reserved");
static_assert(offsetof(EFI_BOOT_SERVICES, LoadImage) == 0xC8, "BS.LoadImage");
static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 0xE8, "BS.ExitBootServices");
static_assert(offsetof(EFI_BOOT_SERVICES, Stall) == 0xF8, "BS.Stall");
static_assert(offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 0x100, "BS.SetWatchdogTimer");
static_assert(offsetof(EFI_BOOT_SERVICES, OpenProtocol) == 0x118, "BS.OpenProtocol");
static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 0x140, "BS.LocateProtocol");
static_assert(offsetof(EFI_BOOT_SERVICES, CalculateCrc32) == 0x158, "BS.CalculateCrc32");
static_assert(offsetof(EFI_BOOT_SERVICES, CreateEventEx) == 0x170, "BS.CreateEventEx");

static_assert(sizeof(EFI_INPUT_KEY) == 4, "EFI_INPUT_KEY");
static_assert(sizeof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL) == 24, "SIMPLE_TEXT_INPUT");
static_assert(offsetof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL, ReadKeyStroke) == 8, "STIP.ReadKeyStroke");
static_assert(offsetof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL, WaitForKey) == 16, "STIP.WaitForKey");
static_assert(sizeof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL) == 80, "SIMPLE_TEXT_OUTPUT");
static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, OutputString) == 8, "STOP.OutputString");
static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, TestString) == 16, "STOP.TestString");
static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, SetAttribute) == 40, "STOP.SetAttribute");
static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, ClearScreen) == 48, "STOP.ClearScreen");
static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, Mode) == 72, "STOP.Mode");
static_assert(sizeof(SIMPLE_TEXT_OUTPUT_MODE) == 24, "SIMPLE_TEXT_OUTPUT_MODE");

static_assert(sizeof(EFI_LOADED_IMAGE_PROTOCOL) == 0x60, "EFI_LOADED_IMAGE_PROTOCOL");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, ParentHandle) == 0x08, "LIP.ParentHandle");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, SystemTable) == 0x10, "LIP.SystemTable");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, DeviceHandle) == 0x18, "LIP.DeviceHandle");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptionsSize) == 0x30, "LIP.LoadOptionsSize");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptions) == 0x38, "LIP.LoadOptions");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, ImageBase) == 0x40, "LIP.ImageBase");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, ImageSize) == 0x48, "LIP.ImageSize");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, ImageCodeType) == 0x50, "LIP.ImageCodeType");
static_assert(offsetof(EFI_LOADED_IMAGE_PROTOCOL, Unload) == 0x58, "LIP.Unload");

#endif
