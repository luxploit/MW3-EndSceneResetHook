// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <cstdio>
#include <iostream>
#include "detours.h"
#include "detver.h"
#include <Psapi.h>
#include <d3d9.h>
#include <mutex>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d3d9.lib")

/*
*   MW3-EndSceneResetHook
*   For educational purposes only!
*   Created by github.com/luxploit-old
*   POC of an EndScene and Reset hook inside MW3
*/

bool mouse_control = false;

MODULEINFO GetModInfo(LPCSTR mod) {
    MODULEINFO mI{ 0 };
    HMODULE hM = GetModuleHandleA(mod);
    if (!hM)
        return mI;
    GetModuleInformation(GetCurrentProcess(), hM, &mI, sizeof(MODULEINFO));
    return mI;
}

DWORD ScanMaskedSig(const char* mod, const char* ptr, const char* mask) {
    MODULEINFO mI = GetModInfo(mod);
    DWORD bE = reinterpret_cast<DWORD>(mI.lpBaseOfDll);
    DWORD sE = mI.SizeOfImage;
    DWORD pL = static_cast<DWORD>(strlen(mask));

    for (auto ix = 0; ix < sE - pL; ix++) {
        bool fn = true;
        for (auto iy = 0; iy < pL; iy++) {
            fn &= mask[iy] == '?' || ptr[iy] == *reinterpret_cast<char*>(bE + ix + iy);
        }
        if (fn)
            return bE + ix;
    }
    return 0xDEADBEEF;
}

HWND hwnd;
void InitDX(IDirect3DDevice9* device) {
    std::cout << "InitDX Call\n";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsClassic();
    ImGui_ImplDX9_Init(device);
    ImGui_ImplWin32_Init(hwnd);
}

std::once_flag callDX;
std::once_flag callEndScene;
using typedef_endScene = HRESULT(NTAPI*)(IDirect3DDevice9* pd);
typedef_endScene og_endScene;
HRESULT NTAPI endScene(IDirect3DDevice9* pd) {
    std::call_once(callDX, InitDX, pd);
    std::call_once(callEndScene, [&] {
        std::cout << "endScene call\n";
    });

    if (mouse_control) {
        ImGui::GetIO().MouseDrawCursor = true;
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    else {
        ImGui::GetIO().MouseDrawCursor = false;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("MW3");
    ImGui::Button("major tom can you hear me?");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return og_endScene(pd);
}

std::once_flag callReset;
using typedef_reset = HRESULT(NTAPI*)(IDirect3DDevice9* pd, D3DPRESENT_PARAMETERS* ppp);
typedef_reset og_reset;
HRESULT NTAPI reset(IDirect3DDevice9* pd, D3DPRESENT_PARAMETERS* ppp) {
    std::call_once(callReset, [&] {
        std::cout << "reset call\n";
    });
    
    auto og = og_reset(pd, ppp);
    ImGui_ImplDX9_CreateDeviceObjects();
    og;
    ImGui_ImplDX9_InvalidateDeviceObjects();
    return og;
}

std::once_flag callWnd;
WNDPROC og_wndProc;
IMGUI_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT NTAPI wndProc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    std::call_once(callWnd, [&] {
        std::cout << "wndProc call\n";
    });

    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(window, msg, wp, lp);

    return CallWindowProcW(og_wndProc, window, msg, wp, lp);
}

#define TableSize 119
DWORD vTableAddr = NULL;
void* Vtable[TableSize];
void HackThread(HMODULE mod) {
    hwnd = FindWindowA(nullptr, "Call of Duty\xAE: Modern Warfare\xAE 3 Multiplayer");

    AllocConsole();
    freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
    std::cout << "MW3-EndSceneResetHook!\n";

    vTableAddr = ScanMaskedSig("d3d9.dll", "\xC7\x06\x00\x00\x00\x00\x89\x86\x00\x00\x00\x00\x89\x86", "xx????xx????xx") + 2;

    #define EndSceneIndex 42
    #define EndSceneEntry Vtable[EndSceneIndex]

    #define ResetIndex 16
    #define ResetEntry Vtable[ResetIndex]

    std::cout << "Vtable addr: " << std::hex << vTableAddr << "\n";
    memcpy((void**)Vtable, *(void***)vTableAddr, sizeof(Vtable));
    std::cout << "endScene addr: " << std::hex << EndSceneEntry << "\n";
    std::cout << "reset addr: " << std::hex << ResetEntry << "\n";

    og_endScene = (typedef_endScene)EndSceneEntry;
    if (!FAILED(DetourTransactionBegin()))
        std::cout << "endScene Detour Transaction Begin\n";
    if (!FAILED(DetourUpdateThread(GetCurrentThread())))
        std::cout << "endScene Detour Update Thread\n";
    if (!FAILED(DetourAttach(&(LPVOID&)og_endScene, endScene)))
        std::cout << "endScene Detour Attach\n";
    if (!FAILED(DetourTransactionCommit()))
        std::cout << "endScene Detour Transaction Commit\n";

    og_reset = (typedef_reset)ResetEntry;
    if (!FAILED(DetourTransactionBegin()))
        std::cout << "reset Detour Transaction Begin\n";
    if (!FAILED(DetourUpdateThread(GetCurrentThread())))
        std::cout << "reset Detour Update Thread\n";
    if (!FAILED(DetourAttach(&(LPVOID&)og_reset, reset)))
        std::cout << "reset Detour Attach\n";
    if (!FAILED(DetourTransactionCommit()))
        std::cout << "reset Detour Transaction Commit\n";

    og_wndProc = (WNDPROC)SetWindowLongW(hwnd, -4 /* GWL_WNDPROC */, (LONG)wndProc);
    std::cout << "wndProc addr: " << std::hex << og_wndProc << "\n";

    while (true) { 
        std::this_thread::sleep_for(std::chrono::microseconds(10));     
        if (GetAsyncKeyState(88) & 0x1)
            mouse_control ^= true;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
            CloseHandle(CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)HackThread, hModule, NULL, NULL));
        return TRUE;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

