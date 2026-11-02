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

DWORD WINAPI InitThread(LPVOID lpParam) {
    Log("InitThread started. Waiting for mhfo.dll...");
    uintptr_t base = 0;
    while (!base) {
        base = (uintptr_t)GetModuleHandleA("mhfo.dll");
        Sleep(100);
    }
    
    char buf[256];
    sprintf(buf, "mhfo.dll found at base address: 0x%08X", base);
    Log(buf);

    DWORD oldProtect;
    // CORRECTED OFFSET: base + 0x98fd
    void* limiterAddr = (void*)(base + 0x98fd);
    
    sprintf(buf, "Attempting to patch frame limiter at 0x%08X", (uintptr_t)limiterAddr);
    Log(buf);

    if (VirtualProtect(limiterAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        uint8_t* limiter = (uint8_t*)limiterAddr;
        limiter[1] = 0x01; // Change 0x21 (33ms) to 0x01 (1ms)
        VirtualProtect(limiterAddr, 5, oldProtect, &oldProtect);
        Log("SUCCESS: Frame limiter patched (30 FPS -> 60+ FPS).");
    } else {
        sprintf(buf, "FAILED to patch frame limiter. GetLastError: %lu", GetLastError());
        Log(buf);
    }

    Log("InitThread finished.");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Log("--- Proxy d3d8.dll Loaded ---");
        
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
        return Real_Direct3DCreate8(SDKVersion);
    }
    return nullptr;
}

extern "C" __declspec(dllexport) void WINAPI DebugSetMute() {}
extern "C" __declspec(dllexport) int WINAPI ValidatePixelShader() { return 0; }
extern "C" __declspec(dllexport) int WINAPI ValidateVertexShader() { return 0; }
extern "C" __declspec(dllexport) void WINAPI Direct3D8EnableMaximizedWindowedModeShim() {}