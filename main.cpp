// Dear ImGui: standalone example application for DirectX 9
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui/imgui.h"
#include "backend/imgui_impl_dx9.h"
#include "backend/imgui_impl_win32.h"
#include <windows.h>
#include <d3d9.h>
#include <tchar.h>

#include <string>
#include <APTAPI.h>

#pragma comment(lib, "APT.lib")

// Data
static LPDIRECT3D9 g_pD3D = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef struct
{
    long serNum;
    float minVel;
    float set_minVel;
    float maxVel;
    float set_maxVel;
    float Accel;
    float set_Accel;
    float limMaxAccel;
    float limMaxVel;
    float curPos;
    float destPos;
    float lastPos; // used to detect home position
    float homeVel;
    float ofst;
    BOOL moving;
    bool warn;
} motorProps;

bool init = true;
bool failed = false;
std::string failmsg = "";
long numUnits = 0;

motorProps *motors = nullptr;
std::string *warnText = nullptr;

DWORD WINAPI InitThreadFcn(LPVOID _in)
{
    while (init)
    {
        // perform init tasks
        long ret = APTInit();
        if (ret)
        {
            init = false;
            failed = true;
            failmsg = "Failed to initialize APT library.";
            continue;
        }
        ret = GetNumHWUnitsEx(HWTYPE_KST101, &numUnits);
        if (ret)
        {
            init = false;
            failed = true;
            failmsg = "Failed to enumerate K-Cubes.";
            continue;
        }
        else if (numUnits == 0)
        {
            init = false;
            failed = true;
            failmsg = "Could not find any K-Cubes.";
            continue;
        }
        motors = new motorProps[numUnits];
        warnText = new std::string[numUnits];
        if (motors == nullptr)
        {
            init = false;
            failed = true;
            failmsg = "Failed to allocate memory for units.";
            continue;
        }
        memset(motors, 0x0, sizeof(motorProps) * numUnits);
        for (long i = 0; i < numUnits; i++)
        {
            ret = GetHWSerialNumEx(HWTYPE_KST101, i, &motors[i].serNum);
            if (ret)
            {
                init = false;
                failed = true;
                failmsg = "Failed to get serial number for device " + std::to_string(i);
                continue;
            }
            ret = InitHWDevice(motors[i].serNum);
            if (ret)
            {
                init = false;
                failed = true;
                failmsg = "Failed to init device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                continue;
            }
        }
        init = false;
    }
    return NULL;
}

BOOL WindowPositionGet(HWND h, RECT *rect)
{
    BOOL retval = true;
    RECT wrect;
    retval &= GetWindowRect(h, &wrect);
    RECT crect;
    retval &= GetClientRect(h, &crect);
    POINT lefttop = {crect.left, crect.top}; // Practicaly both are 0
    ClientToScreen(h, &lefttop);
    POINT rightbottom = {crect.right, crect.bottom};
    ClientToScreen(h, &rightbottom);

    // int left_border = lefttop.x - wrect.left;              // Windows 10: includes transparent part
    // int right_border = wrect.right - rightbottom.x;        // As above
    // int bottom_border = wrect.bottom - rightbottom.y;      // As above
    // int top_border_with_title_bar = lefttop.y - wrect.top; // There is no transparent part
    rect->left = lefttop.x;
    rect->right = rightbottom.x;
    rect->top = lefttop.y;
    rect->bottom = rightbottom.y;
    return retval;
}

// Main code
#ifdef DEBUG_CONSOLE
int main(int, char **)
#else
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
#endif
{
    // Create application window
    // ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Example"), NULL};
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Thorlabs Kinesis KST-101 Controller"), WS_OVERLAPPEDWINDOW, 100, 100, 532, 440, NULL, NULL, wc.hInstance, NULL);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    // IM_ASSERT(font != NULL);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    bool firstRun = true;
    DWORD InitThreadId;
    HANDLE InitThreadHdl = CreateThread(NULL, 0, InitThreadFcn, 0, 0, &InitThreadId);
    if (InitThreadHdl == NULL)
    {
        init = false;
        failed = true;
        failmsg = "Could not start initialization thread";
    }
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (init)
        {
            static int frameCtr = 0;
            static char initBuf[] = {'-', '\\', '|', '/'};
            ImGui::Begin("Control Panel", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            RECT rect;
            int width, height;
            if (WindowPositionGet(hwnd, &rect))
            {
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;
            }
            ImGui::SetWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            int idx = frameCtr / 15;
            idx = idx % 4;
            ImGui::Text("Initializing %c", initBuf[idx]);
            frameCtr++;

            ImGui::End();
        }
        else if (failed)
        {
            ImGui::Begin("Error", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            RECT rect;
            int width, height;
            if (WindowPositionGet(hwnd, &rect))
            {
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;
            }
            ImGui::SetWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::Text(failmsg.c_str());
            if (ImGui::Button("Exit"))
            {
                done = 1;
                continue;
            }
            ImGui::End();
        }
        else
        {
            ImGui::Begin("Control Panel", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            RECT rect;
            int width, height;
            if (WindowPositionGet(hwnd, &rect))
            {
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;
            }
            ImGui::SetWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            if (firstRun) // get info
            {
                long homeDir, limSw;
                for (long i = 0; i < numUnits; i++)
                {
                    long ret = MOT_GetHomeParams(motors[i].serNum, &homeDir, &limSw, &motors[i].homeVel, &motors[i].ofst);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Failed to get home info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    }
                    ret = MOT_GetPosition(motors[i].serNum, &motors[i].curPos);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Failed to get current pos info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    }
                    motors[i].lastPos = motors[i].curPos;
                    motors[i].destPos = motors[i].lastPos;
                    ret = MOT_GetVelParamLimits(motors[i].serNum, &motors[i].limMaxAccel, &motors[i].limMaxVel);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Failed to get velocity limit info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    }
                    ret = MOT_GetVelParams(motors[i].serNum, &motors[i].minVel, &motors[i].Accel, &motors[i].maxVel);
                    {
                        motors[i].warn = true;
                        warnText[i] = "Failed to get velocity params info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    }
                    motors[i].set_Accel = motors[i].Accel;
                    motors[i].set_minVel = motors[i].minVel;
                    motors[i].set_maxVel = motors[i].maxVel;
                }
                firstRun = false;
            }
            for (long i = 0; i < numUnits; i++)
            {
                long ret;
                if (motors[i].moving)
                {
                    ret = MOT_GetPosition(motors[i].serNum, &motors[i].curPos);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Failed to get moving status info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    }
                }
                ret = MOT_GetInMotion(motors[i].serNum, &motors[i].moving);
                bool updateVel = false;
                if (ret)
                {
                    failed = true;
                    failmsg = "Failed to get moving status info for device " + std::to_string(i) + ": " + std::to_string(motors[i].serNum);
                    continue;
                }
                ImGui::Separator();
                ImGui::Text("Motor: %d | Serial: %ld", i + 1, motors[i].serNum);
                // Velocities
                ImGui::Text("Velocity parameters");
                ImGui::Text("Min: %f | Max: %f | Accel: %f", motors[i].minVel, motors[i].maxVel, motors[i].Accel);
                ImGui::Text("Set Velocity Parameters");
                ImGui::Columns(3);
                if (ImGui::InputFloat((std::string("Min##Vel") + std::to_string(i)).c_str(),
                                      &motors[i].set_minVel, 0, 0, "%.3f", motors[i].moving ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    updateVel = true;
                }
                ImGui::NextColumn();
                if (ImGui::InputFloat((std::string("Max##Vel") + std::to_string(i)).c_str(),
                                      &motors[i].set_maxVel, 0, 0, "%.3f", motors[i].moving ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    updateVel = true;
                }
                ImGui::NextColumn();
                if (ImGui::InputFloat((std::string("Accel##Vel") + std::to_string(i)).c_str(),
                                      &motors[i].set_Accel, 0, 0, "%.3f", motors[i].moving ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    updateVel = true;
                }
                ImGui::Columns(1);
                if (updateVel)
                {
                    updateVel = false;
                    ret = MOT_SetVelParams(motors[i].serNum,
                                           motors[i].set_minVel,
                                           motors[i].set_Accel,
                                           motors[i].set_maxVel);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Could not set velocity parameters: " + std::to_string(ret);
                    }
                    else if ((ret = MOT_GetVelParams(motors[i].serNum,
                                                     &motors[i].minVel,
                                                     &motors[i].Accel,
                                                     &motors[i].maxVel)))
                    {
                        motors[i].warn = true;
                        warnText[i] = "Could not retrieve velocity parameters: " + std::to_string(ret);
                    }
                }
                ImGui::Text("Current position: %f", motors[i].curPos);
                if (ImGui::InputFloat((std::string("Destination##") + std::to_string(i)).c_str(), &motors[i].destPos, 0, 0, "%.3f", motors[i].moving ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    ret = MOT_MoveAbsoluteEx(motors[i].serNum, motors[i].destPos, false);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Could not go to position: " + std::to_string(ret);
                    }
                }
                if (ImGui::Button((std::string("Go Home##") + std::to_string(i)).c_str()) && !(motors[i].moving))
                {
                    ret = MOT_MoveHome(motors[i].serNum, false);
                    if (ret)
                    {
                        motors[i].warn = true;
                        warnText[i] = "Could not go home: " + std::to_string(ret);
                    }
                }
                if (motors[i].warn)
                {

                    ImGui::Text("Status: %s", warnText[i].c_str());
                    ImGui::SameLine();
                    if (ImGui::Button((std::string("OKAY##") + std::to_string(i)).c_str()))
                    {
                        motors[i].warn = false;
                    }
                }
                else
                {
                    ImGui::Text("Status: OKAY");
                }
                ImGui::Separator();
            }
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

        // Handle loss of D3D9 device
        if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }
    WaitForSingleObject(InitThreadHdl, 10000);
    CloseHandle(InitThreadHdl);
    if (motors != nullptr)
        delete[] motors;
    if (warnText != nullptr)
        delete[] warnText;
    APTCleanUp();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Present with vsync
    // g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
    if (g_pD3D)
    {
        g_pD3D->Release();
        g_pD3D = NULL;
    }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
