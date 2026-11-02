#include <windows.h>
#include <stdint.h>
#include <stdio.h>

// --- Logging ---
void Log(const char* msg) {
    FILE* f = fopen("mhfz_proxy.log", "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

// --- DXVK Forwarding ---
typedef void* (WINAPI* Direct3DCreate8_t)(UINT SDKVersion);
Direct3DCreate8_t Real_Direct3DCreate8 = nullptr;

DWORD WINAPI InitThread(LPVOID lpParam) {
    Log("InitThread started. Waiting for mhfo.dll...");
    uintptr_t base = 0;
    while (!base) {
        base = (uintptr_t)GetModuleHandleA("mhfo.dll");
        Sleep(100);
    }
    Log("mhfo.dll found! Applying patches...");

    DWORD oldProtect;
    // Patch the Frame Limiter (FUN_100098f0)
    // Original: BE 21 00 00 00 (MOV ESI, 0x21)
    uint8_t* limiter = (uint8_t*)(base + 0x100098fd);
    if (VirtualProtect(limiter, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        limiter[1] = 0x01; // Change 0x21 to 0x01
        VirtualProtect(limiter, 5, oldProtect, &oldProtect);
        Log("Frame limiter patched successfully (30 FPS -> 60+ FPS).");
    } else {
        Log("FAILED to patch frame limiter (VirtualProtect failed).");
    }

    Log("InitThread finished.");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Log("--- Proxy d3d8.dll Loaded ---");
        
        // Load DXVK's d3d8.dll (renamed to dxvk_d3d8.dll)
        HMODULE realD3D8 = LoadLibraryA("dxvk_d3d8.dll");
        if (realD3D8) {
            Real_Direct3DCreate8 = (Direct3DCreate8_t)GetProcAddress(realD3D8, "Direct3DCreate8");
            Log("dxvk_d3d8.dll loaded successfully.");
        } else {
            Log("ERROR: Failed to load dxvk_d3d8.dll!");
        }

        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate8(UINT SDKVersion) {
    Log("Direct3DCreate8 called by game.");
    if (Real_Direct3DCreate8) {
        void* res = Real_Direct3DCreate8(SDKVersion);
        if (res) Log("DXVK returned a valid IDirect3D8 interface.");
        else Log("ERROR: DXVK returned NULL for IDirect3D8.");
        return res;
    }
    Log("ERROR: Real_Direct3DCreate8 is null!");
    return nullptr;
}

extern "C" __declspec(dllexport) void WINAPI DebugSetMute() {}
extern "C" __declspec(dllexport) int WINAPI ValidatePixelShader() { return 0; }
extern "C" __declspec(dllexport) int WINAPI ValidateVertexShader() { return 0; }
extern "C" __declspec(dllexport) void WINAPI Direct3D8EnableMaximizedWindowedModeShim() {}