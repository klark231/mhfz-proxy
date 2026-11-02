#include <windows.h>
#include <stdint.h>
#include <stdio.h>

void Log(const char* msg) {
    FILE* f = fopen("mhfz_proxy.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

typedef void* (WINAPI* Direct3DCreate8_t)(UINT SDKVersion);
Direct3DCreate8_t Real_Direct3DCreate8 = nullptr;

typedef void (__cdecl* tLogicUpdate)();
tLogicUpdate Original_LogicUpdate = nullptr;
int frameCounter = 0;
bool isInitialized = false;

// The exact addresses of the 21 MOV instructions in FUN_11514c60
uintptr_t timerAddrs[] = {
    0x1514f91, 0x1514fa7, 0x1514fbd, 0x1514fd3, 0x1514fe9, 0x1514fff,
    0x1515015, 0x151502b, 0x1515041, 0x1515057, 0x151506d, 0x1515083,
    0x1515099, 0x15150af, 0x15150c5, 0x15150db, 0x15150f1, 0x1515107,
    0x151511d, 0x1515133, 0x1515149
};

void __cdecl Hooked_LogicUpdate() {
    frameCounter++;
    bool skipTimers = (frameCounter % 2 != 0);
    uintptr_t base = (uintptr_t)GetModuleHandleA("mhfo.dll");
    
    if (skipTimers) {
        // Change the decrement from -1 (0xFFFF) to 0 (0x0000)
        for (int i = 0; i < 21; i++) {
            *(uint16_t*)(base + timerAddrs[i]) = 0x0000;
        }
    }
    
    // Run the original function via the trampoline
    Original_LogicUpdate();
    
    if (skipTimers) {
        // Restore the decrement back to -1 (0xFFFF)
        for (int i = 0; i < 21; i++) {
            *(uint16_t*)(base + timerAddrs[i]) = 0xFFFF;
        }
    }
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = 0;
    while (!base) {
        base = (uintptr_t)GetModuleHandleA("mhfo.dll");
        Sleep(100);
    }
    
    DWORD oldProtect;

    // 1. Patch Limiter to 0x10 (16ms = 60 FPS)
    uint8_t* limiter = (uint8_t*)(base + 0x98fd);
    if (VirtualProtect(limiter, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        limiter[1] = 0x10; 
        VirtualProtect(limiter, 5, oldProtect, &oldProtect);
        Log("SUCCESS: Frame limiter patched to 16ms (60 FPS).");
    }

    // 2. Hook the beginning of FUN_11514c60 using a 9-byte Trampoline
    void* hookAddr = (void*)(base + 0x1514c60);
    int hookSize = 9; // 1 byte (PUSH EBP) + 2 bytes (MOV EBP,ESP) + 6 bytes (SUB ESP, 0x414)
    
    void* trampoline = VirtualAlloc(NULL, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (trampoline) {
        // Copy 9 bytes of the original function to the trampoline
        memcpy(trampoline, hookAddr, hookSize);
        
        // Add a JMP instruction in the trampoline to jump back to the original function (hookAddr + 9)
        *(BYTE*)((uintptr_t)trampoline + hookSize) = 0xE9;
        uintptr_t jumpBack = ((uintptr_t)hookAddr + hookSize) - ((uintptr_t)trampoline + hookSize + 5);
        *(uintptr_t*)((uintptr_t)trampoline + hookSize + 1) = jumpBack;
        
        Original_LogicUpdate = (tLogicUpdate)trampoline;

        // Overwrite the original function with a JMP to our hook
        if (VirtualProtect(hookAddr, hookSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            uintptr_t relativeAddr = (uintptr_t)&Hooked_LogicUpdate - ((uintptr_t)hookAddr + 5);
            *(BYTE*)hookAddr = 0xE9; 
            *(uintptr_t*)((uintptr_t)hookAddr + 1) = relativeAddr;
            
            // Pad the remaining 4 bytes with NOP (0x90) to keep the 9 bytes clean
            for (int i = 5; i < hookSize; i++) {
                *(BYTE*)((uintptr_t)hookAddr + i) = 0x90;
            }
            
            VirtualProtect(hookAddr, hookSize, oldProtect, &oldProtect);
            Log("SUCCESS: Hooked LogicUpdate function with 9-byte trampoline.");
        }
    }

    // Make sure the timer memory addresses are writable
    VirtualProtect((void*)(base + 0x1514f91), 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        if (isInitialized) return TRUE; // Prevent double-hooking if DLL loads twice
        isInitialized = true;
        
        DisableThreadLibraryCalls(hModule);
        HMODULE realD3D8 = LoadLibraryA("dxvk_d3d8.dll");
        if (realD3D8) {
            Real_Direct3DCreate8 = (Direct3DCreate8_t)GetProcAddress(realD3D8, "Direct3DCreate8");
        }
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate8(UINT SDKVersion) {
    if (Real_Direct3DCreate8) return Real_Direct3DCreate8(SDKVersion);
    return nullptr;
}
extern "C" __declspec(dllexport) void WINAPI DebugSetMute() {}
extern "C" __declspec(dllexport) int WINAPI ValidatePixelShader() { return 0; }
extern "C" __declspec(dllexport) int WINAPI ValidateVertexShader() { return 0; }
extern "C" __declspec(dllexport) void WINAPI Direct3D8EnableMaximizedWindowedModeShim() {}