#include <windows.h>
#include <stdint.h>
#include <stdio.h>

void Log(const char* msg) {
    FILE* f = fopen("mhfz_proxy.log", "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

typedef void* (WINAPI* Direct3DCreate8_t)(UINT SDKVersion);
Direct3DCreate8_t Real_Direct3DCreate8 = nullptr;

// Hook variables
typedef void (__cdecl* tLogicUpdate)();
tLogicUpdate Original_LogicUpdate = nullptr;
int frameCounter = 0;

// This runs every time the main loop tries to call 0x11514c60
void __cdecl Hooked_LogicUpdate() {
    frameCounter++;
    // Only run the logic (timers, cooldowns) every OTHER frame
    if (frameCounter % 2 == 0) {
        Original_LogicUpdate();
    }
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = 0;
    while (!base) {
        base = (uintptr_t)GetModuleHandleA("mhfo.dll");
        Sleep(100);
    }
    
    char buf[256];
    DWORD oldProtect;

    // 1. Patch Limiter to 0x10 (16ms = 60 FPS)
    uint8_t* limiter = (uint8_t*)(base + 0x98fd);
    if (VirtualProtect(limiter, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        limiter[1] = 0x10; 
        VirtualProtect(limiter, 5, oldProtect, &oldProtect);
        Log("SUCCESS: Frame limiter patched to 16ms (60 FPS).");
    }

    // 2. Hook the CALL instruction at base + 0x8c815c
    void* callSite = (void*)(base + 0x8c815c);
    Original_LogicUpdate = (tLogicUpdate)(base + 0x11514c60);

    if (VirtualProtect(callSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        // Calculate relative address for the CALL instruction
        uintptr_t relativeAddr = (uintptr_t)&Hooked_LogicUpdate - ((uintptr_t)callSite + 5);
        
        *(BYTE*)callSite = 0xE8; // CALL opcode
        *(uintptr_t*)((uintptr_t)callSite + 1) = relativeAddr;

        VirtualProtect(callSite, 5, oldProtect, &oldProtect);
        sprintf(buf, "SUCCESS: Hooked logic loop at 0x%08X", (uintptr_t)callSite);
        Log(buf);
    } else {
        Log("FAILED to hook logic loop.");
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Log("--- Proxy d3d8.dll Loaded ---");
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