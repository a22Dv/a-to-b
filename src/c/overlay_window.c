#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DLL_EXPORT __declspec(dllexport)
#define DX_CALL(obj, func, ...) ((obj)->lpVtbl->func(obj, __VA_ARGS__))
#define REQUIRE(cond, action) \
    do {                      \
        if (!cond) {          \
            action;           \
        }                     \
    } while (0)

#define WINDOW_CLS_NAME "WNCLS_OVLW"
#define WINDOW_FMT_NAME "WNCLS_OVLW_%3d"

#include <Windows.h>
#include <winerror.h>
#include <winuser.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgiformat.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Does not support multi-threading.
static int overlay_count = 0;

typedef struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} BGRA;

/// @brief Overlay Window class.
/// @note Does not support multi-monitor setups. Can only handle the
///       primary display.
typedef struct {
    ID3D11Device *d3ddev;
    IDXGISwapChain1 *swpchain;
    ID3D11DeviceContext *d3dctx;
    ID3D11RenderTargetView *rview;
    ID3D11Texture2D *staging;
    ID3D11ShaderResourceView *srv;
    HWND window_hwnd;
    int cposx;
    int cposy;
    int sx;
    int sy;
    int dx;
    int dy;
} OverlayWindow;

DLL_EXPORT void ovlw_wnclass_init();
DLL_EXPORT void ovlw_wnclass_uninit();
DLL_EXPORT OverlayWindow *ovlw_create(int x, int y, int sx, int sy);
DLL_EXPORT void ovlw_destroy(OverlayWindow *window);
DLL_EXPORT HRESULT ovlw_update(OverlayWindow *window, void *data, int dx, int dy, int ch);
DLL_EXPORT HRESULT ovlw_set_position(OverlayWindow *window, int x, int y);
DLL_EXPORT HRESULT ovlw_set_window_size(OverlayWindow *window, int sx, int sy);
DLL_EXPORT int ovlw_get_position_x(OverlayWindow *window);
DLL_EXPORT int ovlw_get_position_y(OverlayWindow *window);
DLL_EXPORT int ovlw_get_window_height(OverlayWindow *window);
DLL_EXPORT int ovlw_get_window_width(OverlayWindow *window);
DLL_EXPORT int ovlw_get_display_height(OverlayWindow *window);
DLL_EXPORT int ovlw_get_display_width(OverlayWindow *window);
DLL_EXPORT void ovlw_poll_messages(OverlayWindow *window);

HRESULT create_staging_bgra_texture(int sx, int sy, ID3D11Device *device, ID3D11Texture2D **out);
HRESULT create_swap_chain_for_hwnd(
    HWND hwnd, ID3D11Device *device, IDXGIFactory2 *factory, int sx, int sy, IDXGISwapChain1 **out
);
HRESULT create_d3d11device(ID3D11Device **dev, ID3D11DeviceContext **ctx);
HWND create_overlay_window(int x, int y, int sx, int sy);
LRESULT wndprocf(HWND hwnd, UINT ui, WPARAM wp, LPARAM lp);

DLL_EXPORT void ovlw_wnclass_init() {
    WNDCLASSEX wncls = {};
    wncls.cbSize = sizeof(wncls);
    wncls.lpfnWndProc = wndprocf;
    wncls.hInstance = GetModuleHandleA(NULL);
    wncls.lpszClassName = WINDOW_CLS_NAME;
    wncls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassExA(&wncls);
}

DLL_EXPORT void ovlw_wnclass_uninit() {
    UnregisterClassA(WINDOW_CLS_NAME, GetModuleHandleA(NULL));
}

DLL_EXPORT OverlayWindow *ovlw_create(int x, int y, int sx, int sy) {
    /*
     * Function dependency chain goes as follows:
     * CreateWindowExA -> D3D11Device/Context -> DXGIDevice ->
     * DXGIAdapter -> DXGIFactory2 -> SwapChain.
     */

    // Variable declarations set up top due
    // to goto possibly skipping declarations.
    HRESULT hr = S_OK;
    uint8_t prog = 0;
    HWND hwnd = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *dctx = NULL;
    IDXGIDevice *dxgidev = NULL;
    IDXGIAdapter *dxgiadapter = NULL;
    IDXGIFactory2 *dxgifactory2 = NULL;
    IDXGISwapChain1 *swpchain = NULL;
    ID3D11Texture2D *backbuffer = NULL;
    ID3D11Texture2D *staging = NULL;
    OverlayWindow *ovlw = calloc(1, sizeof(OverlayWindow));

    REQUIRE(ovlw && sx > 0 && sy > 0, goto exit);

    hwnd = create_overlay_window(x, y, sx, sy);
    REQUIRE(hwnd, goto exit);

    prog = 1;
    hr = create_d3d11device(&device, &dctx);
    REQUIRE(SUCCEEDED(hr), goto exit);

    // Device -> Adapter (GPU) -> Factory
    prog = 2;
    hr = DX_CALL(device, QueryInterface, &IID_IDXGIDevice, (void **)&dxgidev);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 3;
    hr = DX_CALL(dxgidev, GetParent, &IID_IDXGIAdapter, (void **)&dxgiadapter);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 4;
    hr = DX_CALL(dxgiadapter, GetParent, &IID_IDXGIFactory2, (void **)&dxgifactory2);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 5;
    hr = create_swap_chain_for_hwnd(hwnd, device, dxgifactory2, sx, sy, &swpchain);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 6;
    hr = DX_CALL(swpchain, GetBuffer, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 7;
    hr = create_staging_bgra_texture(sx, sy, device, &staging);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 8;
    REQUIRE(ovlw, goto exit);
    ovlw->window_hwnd = hwnd;
    ovlw->cposx = x;
    ovlw->cposy = y;
    ovlw->sx = sx;
    ovlw->sy = sy;
    ovlw->d3dctx = dctx;
    ovlw->d3ddev = device;
    ovlw->swpchain = swpchain;
    ovlw->dx = GetSystemMetrics(SM_CXSCREEN);
    ovlw->dy = GetSystemMetrics(SM_CYSCREEN);
    ovlw->staging = staging;

    DX_CALL(backbuffer, Release);
    DX_CALL(dxgifactory2, Release);
    DX_CALL(dxgiadapter, Release);
    DX_CALL(dxgidev, Release);
    ++overlay_count;
    return ovlw;
exit:
    switch (prog) {
        case 8: DX_CALL(staging, Release);
        case 7: DX_CALL(backbuffer, Release);
        case 6: DX_CALL(swpchain, Release);
        case 5: DX_CALL(dxgifactory2, Release);
        case 4: DX_CALL(dxgiadapter, Release);
        case 3: DX_CALL(dxgidev, Release);
        case 2: DX_CALL(device, Release);
        case 1: DestroyWindow(hwnd);
        case 0: free(ovlw);
    }
    return (OverlayWindow*)(size_t)hr;
}

DLL_EXPORT void ovlw_destroy(OverlayWindow *window) {
    REQUIRE(window, return);
    DX_CALL(window->rview, Release);
    DX_CALL(window->swpchain, Release);
    DX_CALL(window->d3dctx, Release);
    DX_CALL(window->d3ddev, Release);
    DestroyWindow(window->window_hwnd);
    free(window);
}

/// @brief Updates the texture and shows it to the screen. Assumes data is in BGRA.
DLL_EXPORT HRESULT ovlw_update(OverlayWindow *window, void *data, int dx, int dy, int ch) {
    REQUIRE(window && data && ch == 4, return E_INVALIDARG);
    HRESULT hr = S_OK;

    ID3D11Texture2D *backbuffer = NULL;
    D3D11_MAPPED_SUBRESOURCE sbr = {};
    uint8_t *src = NULL;
    uint8_t *dst = NULL;

    hr = DX_CALL(window->swpchain, GetBuffer, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    REQUIRE(SUCCEEDED(hr), goto exit);
    hr = DX_CALL(
        window->d3dctx, Map, (ID3D11Resource *)window->staging, 0, D3D11_MAP_WRITE, 0, &sbr
    );
    REQUIRE(SUCCEEDED(hr), goto exit);

    src = data;
    dst = sbr.pData;
    if (sbr.RowPitch == dx) {
        memcpy(dst, src, dx * dy * ch);
    } else {
        for (int i = 0; i < dy; ++i) {
            memcpy(dst, src, dx * ch);
            dst += sbr.RowPitch;
            src += dx * ch;
        }
    }
    DX_CALL(window->d3dctx, Unmap, (ID3D11Resource *)window->staging, 0);
    DX_CALL(
        window->d3dctx,
        CopyResource,
        (ID3D11Resource *)backbuffer,
        (ID3D11Resource *)window->staging
    );
    hr = DX_CALL(window->swpchain, Present, 0, 0);
    DX_CALL(backbuffer, Release);
exit:
    return hr;
}

DLL_EXPORT HRESULT ovlw_set_position(OverlayWindow *window, int x, int y) {
    REQUIRE(window, return E_INVALIDARG);
    if (window->cposx == x && window->cposy == y) {
        return S_OK;
    }
    BOOL sp = SetWindowPos(
        window->window_hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    );
    return sp ? S_OK : S_FALSE;
}

DLL_EXPORT HRESULT ovlw_set_window_size(OverlayWindow *window, int sx, int sy) {
    REQUIRE(window, return E_INVALIDARG);
    if (window->sx == sx && window->sy == sy) {
        return S_OK;
    }
    HRESULT hr = S_OK;
    ID3D11Texture2D *backbuffer = NULL;
    BOOL sp = SetWindowPos(
        window->window_hwnd, NULL, 0, 0, sx, sy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );
    REQUIRE(sp, hr = S_FALSE; goto exit);
    DX_CALL(window->rview, Release);
    hr = DX_CALL(
        window->swpchain,
        ResizeBuffers,
        2,
        sx,
        sy,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    );
    REQUIRE(SUCCEEDED(hr), goto exit);
    hr = DX_CALL(window->swpchain, GetBuffer, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    REQUIRE(SUCCEEDED(hr), goto exit);
    hr = DX_CALL(
        window->d3ddev, CreateRenderTargetView, (ID3D11Resource *)backbuffer, NULL, &window->rview
    );
    REQUIRE(SUCCEEDED(hr), goto exit);
    DX_CALL(window->staging, Release);
    hr = create_staging_bgra_texture(window->sx, window->sy, window->d3ddev, &window->staging);
    REQUIRE(SUCCEEDED(hr), goto exit);
    REQUIRE(SUCCEEDED(hr), goto exit);
    window->sx = sx;
    window->sy = sy;
exit:
    return hr;
}

DLL_EXPORT int ovlw_get_position_x(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->cposx;
}

DLL_EXPORT int ovlw_get_position_y(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->cposy;
}

DLL_EXPORT int ovlw_get_window_height(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->sy;
}

DLL_EXPORT int ovlw_get_window_width(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->sx;
}

DLL_EXPORT int ovlw_get_display_height(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->dy;
}

DLL_EXPORT int ovlw_get_display_width(OverlayWindow *window) {
    REQUIRE(window, return 0);
    return window->dx;
}

DLL_EXPORT void ovlw_poll_messages(OverlayWindow *window) {
    MSG msg = {};
    while (PeekMessageA(&msg, window->window_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

HRESULT create_staging_bgra_texture(int sx, int sy, ID3D11Device *device, ID3D11Texture2D **out) {
    D3D11_TEXTURE2D_DESC tdesc = {};
    tdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    tdesc.MipLevels = 1;         // Original texture size.
    tdesc.ArraySize = 1;         // Texture "stack" is of size 1.
    tdesc.SampleDesc.Count = 1;  // No MSAA. (Multi-Sampling anti-aliasing).
    tdesc.SampleDesc.Quality = 0;
    tdesc.Usage = D3D11_USAGE_STAGING;
    tdesc.BindFlags = 0;
    tdesc.Width = sx;
    tdesc.Height = sy;
    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    HRESULT hr = DX_CALL(device, CreateTexture2D, &tdesc, NULL, out);
    return hr;
}

HRESULT create_swap_chain_for_hwnd(
    HWND hwnd, ID3D11Device *device, IDXGIFactory2 *factory, int sx, int sy, IDXGISwapChain1 **out
) {
    /**
     * ERROR: 0x887A0001 (DXGI_ERROR_INVALID_CALL)
     */
    DXGI_SWAP_CHAIN_DESC1 swdsc = {};
    swdsc.Width = sx;
    swdsc.Height = sy;
    swdsc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swdsc.Stereo = 0;                                  // No stereoscopic vision.
    swdsc.SampleDesc.Count = 1;                        // No multi-sampling.
    swdsc.SampleDesc.Quality = 0;                      // Default Quality.
    swdsc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swdsc.BufferCount = 2;                             // Double-buffering.
    swdsc.Scaling = DXGI_SCALING_STRETCH;                 // No scaling.
    swdsc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Pointer-swap.
    swdsc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;   // Use Alpha as-is.
    swdsc.Flags = 0;
    return DX_CALL(
        factory, CreateSwapChainForHwnd, (IUnknown *)device, hwnd, &swdsc, NULL, NULL, out
    );
}

HRESULT create_d3d11device(ID3D11Device **dev, ID3D11DeviceContext **ctx) {
    return D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &(D3D_FEATURE_LEVEL){D3D_FEATURE_LEVEL_11_1},
        1,
        D3D11_SDK_VERSION,
        dev,
        NULL,
        ctx
    );
}

HWND create_overlay_window(int x, int y, int sx, int sy) {
    char wname[16] = {};
    sprintf(wname, WINDOW_FMT_NAME, overlay_count);  // Limit to 999 windows.
    HWND hwnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        WINDOW_CLS_NAME,
        wname,
        WS_POPUP | WS_VISIBLE,
        x,
        y,
        sx,
        sy,
        NULL,
        NULL,
        GetModuleHandleA(NULL),
        NULL
    );
    if (hwnd) {  // Ensures flags set by CreateWindowExA are enforced.
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    }
    return hwnd;
}

LRESULT wndprocf(HWND hwnd, UINT ui, WPARAM wp, LPARAM lp) {
    switch (ui) {
        case WM_ERASEBKGND: return 1;  // Return non-zero to mark handled. (Erase Background)
        case WM_PAINT:
            ValidateRect(hwnd, NULL);
            return 0;  // ValidateRect prevents infinite loop requests. Return zero for handled.
        case WM_NCHITTEST:
            // If WM_TRANSPARENT does not stop cursor hit-test requests.
            return HTTRANSPARENT;
        case WM_DESTROY:
            // Acknowledge message.
            return 0;
        default: return DefWindowProcA(hwnd, ui, wp, lp);  // Default Windows Procedure.
    }
}