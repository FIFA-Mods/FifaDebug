#include "plugin.h"
#include <vector>
#include <filesystem>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx9.h"

using namespace plugin;
using namespace std;
using namespace std::filesystem;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

uintptr_t OrigDirect3DCreate = 0;
uintptr_t OrigDirect3DDestroy = 0;
uintptr_t OrigDirect3DReset = 0;
uintptr_t Original3DRender = 0;
uintptr_t gSportsRNAAddr = 0;
uintptr_t OriginalWndProc = 0;
std::list<std::string> DebugMessages;
std::list<std::wstring> DebugMessagesW;

enum MessageFlags {
    MSG_PRINTF = 1,
    MSG_OUTPUT_DEBUG_STRING = 2,
    MSG_DLC = 4,
    MSG_UGC = 8,
    MSG_FCE_GAME_MODES = 16,
    MSG_USER = 32,
    MSG_ALL = 0xFFFFFFFF
};

unsigned int GetIniFlags() {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"Debug", L"Messages", L"", buf, 1024,
        FIFA::GameDirPath(L"plugins\\FifaDebug.ini").c_str());
    auto messages = ToLower(buf);
    Trim(messages);
    if (messages.empty() || messages == L"all")
        return MSG_ALL;
    auto parts = Split(ToLower(buf), L',', true, true, false);
    unsigned int flags = 0;
    for (auto const &p : parts) {
        if (p == L"printf")
            flags |= MSG_PRINTF;
        else if (p == L"outputdebugstring")
            flags |= MSG_OUTPUT_DEBUG_STRING;
        else if (p == L"dlc")
            flags |= MSG_DLC;
        else if (p == L"ugc")
            flags |= MSG_UGC;
        else if (p == L"fcegamemodes")
            flags |= MSG_FCE_GAME_MODES;
        else if (p == L"user")
            flags |= MSG_USER;
    }
    return flags;
}

unsigned int &Flags() {
    static unsigned int flags = GetIniFlags();
    return flags;
}

void CopyListToClipboard(const std::list<std::string> &strList) {
    if (strList.empty()) return;
    size_t totalSize = 0;
    for (const auto &str : strList)
        totalSize += str.size() + 2;
    totalSize += 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hMem) return;
    char *buffer = static_cast<char *>(GlobalLock(hMem));
    if (!buffer) {
        GlobalFree(hMem);
        return;
    }
    char *ptr = buffer;
    for (const auto &str : strList) {
        memcpy(ptr, str.c_str(), str.size());
        ptr += str.size();
        //*ptr++ = '\r';
        //*ptr++ = '\n';
    }
    *ptr = '\0';
    GlobalUnlock(hMem);
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
    }
    else
        GlobalFree(hMem);
}

static void DrawDebug() {
    static size_t last_size = 0;
    if (ImGui::Button("Clear Log"))
        DebugMessages.clear();
    ImGui::SameLine();
    if (ImGui::Button("Copy Log"))
        CopyListToClipboard(DebugMessages);

    std::string message;
    for (auto const &s : DebugMessages)
        message += s /* + "\n"*/;

    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), true);
    ImGui::TextUnformatted(message.c_str());
    if (message.size() != last_size) {
        ImGui::SetScrollHereY(1.0f);
        last_size = message.size();
    }
    ImGui::EndChild();
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallMethodAndReturnDynGlobal<LRESULT>(OriginalWndProc, 0, hWnd, msg, wParam, lParam);
}

bool OnDirect3DCreate(void *initParams) {
    bool result = CallAndReturnDynGlobal<bool>(OrigDirect3DCreate, initParams);
    if (result) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(*raw_ptr<HWND>(initParams, 4));
        void *rna = *(void **)gSportsRNAAddr;
        ImGui_ImplDX9_Init(*raw_ptr<IDirect3DDevice9 *>(rna, 0xC));
    }
    //Message("Create 3D");
    return result;
}

void *METHOD OnDirect3DDestroy(void *t) {
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    //Message("Destroy 3D");
    return CallMethodAndReturnDynGlobal<void *>(OrigDirect3DDestroy, t);
}

bool METHOD OnDirect3DReset(void *t) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    bool result = CallMethodAndReturnDynGlobal<bool>(OrigDirect3DReset, t);
    ImGui_ImplDX9_CreateDeviceObjects();
    //Message("Reset 3D");
    return result;
}

bool METHOD OnDirect3DRender(void *t, DUMMY_ARG, bool arg) {
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawDebug();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    //Message("Render 3D");
    return CallMethodAndReturnDynGlobal<bool>(Original3DRender, t, arg);
}

void MyDebugPrint(std::string const &message) {
    while (DebugMessages.size() >= 1000)
        DebugMessages.pop_front();
    DebugMessages.push_back(message);
}

void MyPrintf(char const *format, ...) {
    va_list myargs;
    va_start(myargs, format);
    static char buf[2048];
    buf[0] = '\0';
    vsprintf(buf, format, myargs);
    MyDebugPrint(buf);
    va_end(myargs);
}

void MyPrintfDLC(char const *format, ...) {
    if (!strncmp(format, "[DLC] Update", 12))
        return;
    va_list myargs;
    va_start(myargs, format);
    static char buf[2048];
    buf[0] = '\0';
    vsprintf(buf, format, myargs);
    MyDebugPrint(buf);
    va_end(myargs);
}

void MyPrintfUGC(char const *format, ...) {
    va_list myargs;
    va_start(myargs, format);
    static char buf[2048];
    buf[0] = '\0';
    vsprintf(buf, format, myargs);
    MyDebugPrint(buf);
    va_end(myargs);
}

void MyPrintfDUI(int, char const *format, ...) {
    va_list myargs;
    va_start(myargs, format);
    static char buf[2048];
    buf[0] = '\0';
    vsprintf(buf, format, myargs);
    MyDebugPrint(buf);
    va_end(myargs);
}

void __stdcall MyOutputDebugStringA(LPCSTR lpOutputString) {
    MyDebugPrint(lpOutputString);
}

extern "C" __declspec(dllexport) void DebugPrint(std::string const &message) {
    if (Flags() & MSG_USER)
        MyDebugPrint(message);
}

class FifaDebug {
public:
    FifaDebug() {
        if (!CheckPluginName(Magic<'F','i','f','a','D','e','b','u','g','.','a','s','i'>()))
            return;
        auto v = FIFA::GetAppVersion();
        switch (v.id()) {
        case ID_FIFA13_1700_RLD:
            OrigDirect3DCreate = patch::RedirectCall(0x1292AD1, OnDirect3DCreate);
            OrigDirect3DDestroy = patch::RedirectCall(0x19FD006, OnDirect3DDestroy);
            patch::RedirectCall(0x1A0FEF4, OnDirect3DDestroy);
            OrigDirect3DReset = patch::RedirectCall(0x19D4A9D, OnDirect3DReset);
            patch::RedirectCall(0x1BB12B0, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x313B308);
            patch::SetPointer(0x313B308, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0x12A5176 + 3);
            patch::SetPointer(0x12A5176 + 3, WndProc);
            gSportsRNAAddr = 0x3DD53F0;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x2F36588, MyPrintf);
            if (Flags() & MSG_DLC)
                patch::RedirectJump(0x531870, MyPrintfDLC);
            if (Flags() & MSG_UGC)
                patch::RedirectJump(0xFABB70, MyPrintfUGC);
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x2F36270, MyOutputDebugStringA);
            if (Flags() & MSG_FCE_GAME_MODES)
                patch::RedirectJump(0x749090, MyPrintfDUI);
            break;
        case ID_FIFA13_1800:
            OrigDirect3DCreate = patch::RedirectCall(0x128E051, OnDirect3DCreate);
            OrigDirect3DDestroy = patch::RedirectCall(0x19F8746, OnDirect3DDestroy);
            patch::RedirectCall(0x1A0B614, OnDirect3DDestroy);
            OrigDirect3DReset = patch::RedirectCall(0x19D01BD, OnDirect3DReset);
            patch::RedirectCall(0x1BAC9E0, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x25333D8);
            patch::SetPointer(0x25333D8, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0x12A21F6 + 3);
            patch::SetPointer(0x12A21F6 + 3, WndProc);
            gSportsRNAAddr = 0x27A1D60;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x232E58C, MyPrintf);
            if (Flags() & MSG_DLC)
                patch::RedirectJump(0x52C920, MyPrintfDLC);
            if (Flags() & MSG_UGC)
                patch::RedirectJump(0xFA7130, MyPrintfUGC);
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x232E270, MyOutputDebugStringA);
            if (Flags() & MSG_FCE_GAME_MODES)
                patch::RedirectJump(0x744110, MyPrintfDUI);
            break;
        case ID_FIFA12_1700:
            OrigDirect3DCreate = patch::RedirectCall(0xE55E72, OnDirect3DCreate);
            OrigDirect3DReset = patch::RedirectCall(0xDF7C19, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x1764674);
            patch::SetPointer(0x1764674, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0xB9E923 + 4);
            patch::SetPointer(0xB9E923 + 4, WndProc);
            gSportsRNAAddr = 0x1A4AE00;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x1650400, MyPrintf);
            if (Flags() & MSG_DLC)
                patch::RedirectJump(0xB403B0, MyPrintfDLC);
            if (Flags() & MSG_UGC)
                patch::RedirectJump(0xD359E0, MyPrintfUGC);
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x16501C4, MyOutputDebugStringA);
            if (Flags() & MSG_FCE_GAME_MODES)
                patch::RedirectJump(0xCCEC20, MyPrintfDUI);
            break;
        case ID_FIFA12_1500_SKD:
            OrigDirect3DCreate = patch::RedirectCall(0xE52872, OnDirect3DCreate);
            OrigDirect3DReset = patch::RedirectCall(0xDF3079, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x17646E4);
            patch::SetPointer(0x17646E4, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0xB9CB13 + 4);
            patch::SetPointer(0xB9CB13 + 4, WndProc);
            gSportsRNAAddr = 0x1A4ADC0;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x1B7B3FC, MyPrintf);
            if (Flags() & MSG_DLC)
                patch::RedirectJump(0xB3FD90, MyPrintfDLC);
            if (Flags() & MSG_UGC)
                patch::RedirectJump(0xD33CB0, MyPrintfUGC);
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x1B7B1AC, MyOutputDebugStringA);
            if (Flags() & MSG_FCE_GAME_MODES)
                patch::RedirectJump(0xCCDA50, MyPrintfDUI);
            break;
        case ID_FIFA12_1000_RLD:
            OrigDirect3DCreate = patch::RedirectCall(0x80C912, OnDirect3DCreate);
            OrigDirect3DReset = patch::RedirectCall(0x7AF079, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x17389BC);
            patch::SetPointer(0x17389BC, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0x51CF0C + 4);
            patch::SetPointer(0x51CF0C + 4, WndProc);
            gSportsRNAAddr = 0x19A9F70;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x163F410, MyPrintf);
            if (Flags() & MSG_DLC)
                patch::RedirectJump(0x4C3D30, MyPrintfDLC);
            if (Flags() & MSG_UGC)
                patch::RedirectJump(0x6AFBE0, MyPrintfUGC);
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x163F1E0, MyOutputDebugStringA);
            if (Flags() & MSG_FCE_GAME_MODES)
                patch::RedirectJump(0x64E830, MyPrintfDUI);
            break;
        case ID_FIFA11_1010_RLD:
        case ID_FIFA11_1010:
            OrigDirect3DCreate = patch::RedirectCall(0x83A549, OnDirect3DCreate);
            OrigDirect3DReset = patch::RedirectCall(0xEC21D0, OnDirect3DReset);
            Original3DRender = patch::GetUInt(0x12641DC);
            patch::SetPointer(0x12641DC, OnDirect3DRender);
            OriginalWndProc = patch::GetUInt(0x8EA74A + 4);
            patch::SetPointer(0x8EA74A + 4, WndProc);
            gSportsRNAAddr = 0x1489760;
            if (Flags() & MSG_PRINTF)
                patch::SetPointer(0x114A280, MyPrintf);
            if (v.id() != ID_FIFA11_1010) {
                if (Flags() & MSG_DLC)
                    patch::RedirectJump(0x4388C0, MyPrintfDLC);
            }
            if (Flags() & MSG_OUTPUT_DEBUG_STRING)
                patch::SetPointer(0x114A178, MyOutputDebugStringA);
            break;
        }
    }
} fifaDebug;
