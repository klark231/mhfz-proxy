#include <windows.h>
#include <stdint.h>

// --- DXVK Forwarding ---
typedef void* (WINAPI* Direct3DCreate8_t)(UINT SDKVersion);
Direct3DCreate8_t Real_Direct3DCreate8 = nullptr;

// --- Logic Hook ---
typedef void (__cdecl* tLogicUpdate)();
tLogicUpdate Original_LogicUpdate = nullptr;

int frameCounter = 0;
BYTE originalBytes[5];

void __cdecl Hooked_LogicUpdate() {
    frameCounter++;
    // Only run the logic (timers, physics) every other frame
    if (frameCounter % 2 == 0) {
        Original_LogicUpdate();
    }
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = 0;
    // Wait for the game module to load
    while (!base) {
        base = (uintptr_t)GetModuleHandleA("mhfo.dll");
        Sleep(100);
    }

    DWORD oldProtect;

    // 1. Patch the Frame Limiter (FUN_100098f0)
    // Change MOV ESI, 0x21 (33ms) to MOV ESI, 0x01 (1ms)
    uint8_t* limiter = (uint8_t*)(base + 0x100098fd);
    if (VirtualProtect(limiter, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        limiter[1] = 0x01; 
        VirtualProtect(limiter, 5, oldProtect, &oldProtect);
    }

    // 2. Hook the Logic Update (FUN_11514c60) to prevent double speed
    void* hookAddr = (void*)(base + 0x11514c60);
    Original_LogicUpdate = (tLogicUpdate)hookAddr;

    if (VirtualProtect(hookAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(originalBytes, hookAddr, 5); // Save original bytes

        // Calculate relative jump (32-bit x86)
        uintptr_t relativeAddr = (uintptr_t)&Hooked_LogicUpdate - ((uintptr_t)hookAddr + 5);
        *(BYTE*)hookAddr = 0xE9; // JMP
        *(uintptr_t*)((uintptr_t)hookAddr + 1) = relativeAddr;

        VirtualProtect(hookAddr, 5, oldProtect, &oldProtect);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // Load DXVK's d3d8.dll (renamed to dxvk_d3d8.dll)
        HMODULE realD3D8 = LoadLibraryA("dxvk_d3d8.dll");
        if (realD3D8) {
            Real_Direct3DCreate8 = (Direct3DCreate8_t)GetProcAddress(realD3D8, "Direct3DCreate8");
        }

        // Start background thread to wait for game and apply patches
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
