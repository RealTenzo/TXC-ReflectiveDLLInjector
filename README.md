# TXC Injector

Reflective DLL injector with a console UI. You give it a DLL path and a process name, it loads the DLL into that process without touching disk on the target side.

---

## What is reflective injection

Normal `LoadLibrary` injection writes the DLL path into the target process and calls `LoadLibrary` remotely — Windows handles the actual loading. That means the DLL shows up in the module list and leaves a clear trail.

Reflective injection is different. The DLL carries its own loader (`ReflectiveLoader`) as an exported function. The injector copies the raw DLL bytes into the target process's memory, then creates a remote thread pointing at `ReflectiveLoader` inside that allocation. From there the DLL bootstraps itself — finds its own base by scanning back from the instruction pointer, locates `kernel32.dll` through the PEB without using any imports, manually maps its sections, resolves its own IAT, applies relocations, then calls `DllMain`. The OS never knows it loaded anything.

---

## Projects

### TXCInjector
The console that does the injection. Single `.cpp`, no dependencies beyond the Win32 SDK.

### TXCPayload
A DLL template with `ReflectiveLoader` already wired up. Put your code in `DllMain` where the placeholder is, build it, and it works with the injector out of the box.

---

## How to build

Both projects are VS2022 solutions targeting x64. Open the `.sln`, set configuration to `Release | x64`, build.

- `TXCInjector` → produces `TXCInjector.exe`
- `TXCPayload` → produces `TXCPayload.dll`

No NuGet packages, no third party headers, nothing to install.

---

## How to use

1. Build or grab a reflective DLL (one that exports `ReflectiveLoader`)
2. Launch `TXCInjector.exe` as administrator
3. Enter the full path to the DLL when prompted
4. Enter the target process name — e.g. `javaw.exe`, `notepad.exe`
5. The injector finds the process, copies the DLL into it, and fires the loader

If the process isn't running it tells you and asks if you want to try again. Same if the DLL path doesn't exist or the file isn't a reflective DLL.

---

## Requirements

- Windows 10 or later (needs `ENABLE_VIRTUAL_TERMINAL_PROCESSING` for the console colors)
- Run as administrator — `OpenProcess` with `PROCESS_VM_WRITE | PROCESS_CREATE_THREAD` requires it on most targets
- The DLL must export `ReflectiveLoader` — standard DLLs won't work, use the `TXCPayload` template

---

## How the injector works step by step

```
1. Read the DLL file into a local heap buffer
2. Walk the PE export table to find the file offset of ReflectiveLoader
3. Find the target process PID by scanning the snapshot from CreateToolhelp32Snapshot
4. OpenProcess with VM_WRITE + CREATE_THREAD + VM_OPERATION
5. VirtualAllocEx — allocate RW memory in the target, size of the DLL file
6. WriteProcessMemory — copy the raw DLL bytes over
7. VirtualProtectEx — flip the region to RX
8. CreateRemoteThread at (remote base + ReflectiveLoader offset)
9. Wait up to 3 seconds for the thread, then close handles
```

From step 8 onward everything runs inside the target process. The injector's job is done once the thread is created.

---

## How ReflectiveLoader works

The loader has no imports — it can't, because at the point it runs the DLL isn't mapped yet and the IAT isn't resolved. It finds everything it needs manually:

- Scans backward from `_ReturnAddress()` one byte at a time until it hits the `MZ` signature of itself
- Walks `PEB->Ldr->InMemoryOrderModuleList` to find `kernel32.dll` by hash instead of by name
- Resolves `VirtualAlloc`, `LoadLibraryA`, `GetProcAddress`, `FlushInstructionCache` from kernel32's export table using the same hashing approach
- Allocates a new RWX region, copies headers and sections into it
- Resolves the IAT by calling `LoadLibraryA` + `GetProcAddress` for each import
- Applies base relocations if the allocation address differs from the preferred image base
- Flushes the instruction cache and calls `DllMain(DLL_PROCESS_ATTACH)`

API hashing means no function name strings appear in the binary, which makes static analysis harder.

---

## Making your own payload

Open `TXCPayload.sln`. The only file you care about is `dllmain.cpp`. Find this block:

```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // PUT YOUR CODE HERE
        MessageBoxA(NULL, "Injected!", "TXC", MB_OK);
    }
    return TRUE;
}
```

Replace the `MessageBoxA` with whatever you want. If you need a background thread, create one here with `CreateThread`. Don't do heavy work directly in `DllMain` — the loader lock rules still apply since `DllMain` is called normally after the reflective mapping is complete.

Build as `Release | x64`. Hand the resulting DLL path to the injector.

---

## Notes

- 64-bit only. The reflective loader reads the PEB with `__readgsqword(0x60)` which is x64 specific. 32-bit target support would need a separate x86 build of the payload and injector.
- Antivirus will flag this. That's expected — `VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread` is a classic injection pattern regardless of what you're injecting.
- The injector does not clean up the remote allocation after injection. The memory stays mapped because the DLL is running from it.
