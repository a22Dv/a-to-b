/**
 * @brief
 * Custom DXGI Flip-Sequential Window implementation.
 *
 * @warning
 * Does not support multi-monitor displays and
 * scaling != 100%.
 *
 * @remarks
 * - Does not handle DXGI_ERROR_LOST
 * - Caller must check for DXGI_ERROR_WAIT_TIMEOUT and
 *   handle it accordingly.
 * - Performance differs heavily under memory pressure.
 * - Memory pressure / GPU usage heavily affects performance.
 * - Baseline performance, ~0.3ms per (1280x720) frame from caller to data hand-off.
 * - Tested under: (Windows 11, Ryzen 7 8845HS, LPDDR5X-7500, 1080P60).
 */

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcommon.h>
#include <dcomp.h>
#include <dcomptypes.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgiformat.h>
#include <intsafe.h>
#include <windef.h>
#include <winerror.h>
#include <winuser.h>
#include <wrl.h>

#include <cstdio>
#include <new>
#include <random>

#define DLL_EXPORT __declspec(dllexport)
#define RETURN_HR_ON_FAILURE(hr) \
    do {                         \
        if (FAILED(hr)) {        \
            return hr;           \
        }                        \
    } while (false)

namespace WRL = Microsoft::WRL;
constexpr std::size_t window_name_max_len = 64;
constexpr int channel_count = 4;  // BGRA
class Overlay;

extern "C" {

DLL_EXPORT HRESULT create_overlay(Overlay **objptr, int x, int y, int sx, int sy) noexcept;
DLL_EXPORT void destroy_overlay(Overlay **objptr) noexcept;
DLL_EXPORT HRESULT update_overlay(Overlay *obj, void *data, int dx, int dy, int dz) noexcept;
DLL_EXPORT HRESULT reposition_overlay(Overlay *obj, int x, int y) noexcept;
DLL_EXPORT HRESULT resize_overlay(Overlay *obj, int sx, int sy) noexcept;
}

class Overlay {
   public:
    Overlay();
    Overlay(const Overlay &) = delete;
    Overlay operator=(const Overlay &) = delete;
    Overlay(Overlay &&) = delete;
    Overlay operator=(Overlay &&) = delete;
    ~Overlay() noexcept;

    /**
     * @brief
     * Class instance initializer.
     * Creates the window and registers the window class.
     */
    BOOL initialize(int x, int y, int sx, int sy) noexcept;

    /**
     * @brief
     * Message dispatcher. Is required for
     * _wndproc() to be called and receive the messages.
     */
    void dispatch_messages() noexcept;

    /**
     * @brief
     * Repositions the overlay on the screen
     * based on the given coordinates.
     */
    HRESULT reposition_overlay(int x, int y) noexcept;

    /**
     * @brief
     * Resizes the overlay on the screen based on the
     * given dimensions.
     */
    HRESULT resize_overlay(int sx, int sy) noexcept;

    /**
     * @brief
     * Updates the frame displayed on the overlay with the given
     * data. Treats data as B8G8R8A8.
     * @note
     * dx must match the width of the overlay,
     * dy must match the height of the overlay,
     * dz must match the number of channels of the overlay (4, BGRA).
     */
    HRESULT update_overlay(void *data, int dx, int dy, int dz) noexcept;

   private:
    WRL::ComPtr<ID3D11Device> _d3ddevice{};
    WRL::ComPtr<ID3D11DeviceContext> _d3dcontext{};
    WRL::ComPtr<IDCompositionDesktopDevice> _dcompdevice{};
    WRL::ComPtr<IDCompositionTarget> _dcomptarget{};
    WRL::ComPtr<IDCompositionVisual2> _dcompvisual{};
    WRL::ComPtr<IDXGISwapChain1> _swapchain{};
    WRL::ComPtr<ID3D11Texture2D> _staging{};

    HMODULE _module_handle{};
    HWND _hwnd{};
    ATOM _wnclass_atom{};
    char _window_identifier[window_name_max_len]{};
    bool _initialized = false;
    int _window_width = 0;
    int _window_height = 0;

    BOOL _register_window_class() noexcept;
    void _unregister_window_class() noexcept;
    BOOL _create_window_instance(int x, int y, int sx, int sy) noexcept;
    HRESULT _d3d_dcomp_init(int sx, int sy) noexcept;
    HRESULT _create_staging_texture(int sx, int sy) noexcept;
    static LRESULT _wndproc(HWND hwnd, UINT ui, WPARAM wp, LPARAM lp) noexcept;
};

DLL_EXPORT HRESULT create_overlay(Overlay **objptr, int x, int y, int sx, int sy) noexcept {
    if (!objptr) {
        return E_POINTER;
    }
    try {
        auto *obj = new Overlay();
        BOOL initstatus = obj->initialize(x, y, sx, sy);
        if (!initstatus) {
            return E_FAIL;
        }
        *objptr = obj;
    } catch (std::bad_alloc) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

DLL_EXPORT void destroy_overlay(Overlay **objptr) noexcept {
    if (!objptr || !*objptr) {
        return;
    }
    delete *objptr;
    *objptr = nullptr;
}

DLL_EXPORT HRESULT update_overlay(Overlay *obj, void *data, int dx, int dy, int dz) noexcept {
    if (!obj || !data) {
        return E_POINTER;
    }
    return obj->update_overlay(data, dx, dy, dz);
}

DLL_EXPORT HRESULT reposition_overlay(Overlay *obj, int x, int y) noexcept {
    if (!obj) {
        return E_POINTER;
    }
    return obj->reposition_overlay(x, y);
}

DLL_EXPORT HRESULT resize_overlay(Overlay *obj, int sx, int sy) noexcept {
    if (!obj) {
        return E_POINTER;
    }
    return obj->resize_overlay(sx, sy);
}

Overlay::Overlay() {
    std::minstd_rand rand{std::random_device{}()};
    std::sprintf(
        _window_identifier,
        "WNDOVLW-%05X-%05X-%05X",
        rand() % unsigned(1e6),
        rand() % unsigned(1e6),
        rand() % unsigned(1e6)
    );
    _module_handle = GetModuleHandleA(NULL);
}

Overlay::~Overlay() noexcept {
    if (!_initialized) {
        return;
    }
    DestroyWindow(_hwnd);
    _unregister_window_class();
}

BOOL Overlay::initialize(int x, int y, int sx, int sy) noexcept {
    BOOL rt = _register_window_class();
    if (!rt) {
        return rt;
    }
    rt = _create_window_instance(x, y, sx, sy);
    if (!rt) {
        return rt;
    }
    _window_height = sy;
    _window_width = sx;
    HRESULT hr = S_OK;
    if (_hwnd && SUCCEEDED(hr)) {
        hr = _d3d_dcomp_init(sx, sy); 
        if (FAILED(hr)) {
            return hr;
        }
        hr = _create_staging_texture(sx, sy);
        _initialized = SUCCEEDED(hr);
    }
    return _initialized;
}

void Overlay::dispatch_messages() noexcept {
    MSG msg = {};

    // Retrieves all thread-level messages, not just for a specific window.
    while (PeekMessageA(&msg, NULL, 0, UINT_MAX, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

HRESULT Overlay::reposition_overlay(int x, int y) noexcept {
    BOOL rt = SetWindowPos(_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);
    return rt ? S_OK : E_FAIL;
}

HRESULT Overlay::resize_overlay(int sx, int sy) noexcept {
    BOOL rt = SetWindowPos(_hwnd, HWND_TOPMOST, 0, 0, sx, sy, SWP_NOMOVE);
    if (!rt) {
        return E_FAIL;
    }
    _d3dcontext->ClearState();  // Clear references.
    HRESULT hr = _swapchain->ResizeBuffers(0, sx, sy, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    RETURN_HR_ON_FAILURE(hr);
    hr = _create_staging_texture(sx, sy);  // Recreate texture.
    RETURN_HR_ON_FAILURE(hr);
    _window_height = sy;
    _window_width = sx;
    return hr;
}

HRESULT Overlay::update_overlay(void *data, int dx, int dy, int dz) noexcept {
    if (!data || dx != _window_width || dy != _window_height || dz != channel_count) {
        return E_INVALIDARG;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = _d3dcontext->Map(_staging.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    RETURN_HR_ON_FAILURE(hr);
    if (mapped.RowPitch == dx * dz) {
        std::memcpy(mapped.pData, data, dy * dx * dz);
    } else {
        std::uint8_t *src = static_cast<std::uint8_t *>(data);
        std::uint8_t *dst = static_cast<std::uint8_t *>(mapped.pData);
        for (int i = 0; i < dy; ++i) {
            std::memcpy(dst, src, dx * dz);
            src += dx * dz;
            dst += mapped.RowPitch;
        }
    }
    _d3dcontext->Unmap(_staging.Get(), 0);
    WRL::ComPtr<ID3D11Texture2D> backbuffer = {};
    hr = _swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    RETURN_HR_ON_FAILURE(hr);
    _d3dcontext->CopyResource(backbuffer.Get(), _staging.Get());

    // V-Sync Disabled. CPU might take longer than 16.6ms per frame.
    hr = _swapchain->Present(0, 0);
    return hr;
}

BOOL Overlay::_register_window_class() noexcept {
    WNDCLASSA window_class = {};
    window_class.lpszClassName = _window_identifier;
    window_class.lpfnWndProc = _wndproc;
    window_class.hInstance = _module_handle;
    _wnclass_atom = RegisterClassA(&window_class);
    return !!_wnclass_atom;
}

void Overlay::_unregister_window_class() noexcept {
    UnregisterClassA(_window_identifier, _module_handle);
}

BOOL Overlay::_create_window_instance(int x, int y, int sx, int sy) noexcept {
    _hwnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        _window_identifier,
        _window_identifier,
        WS_POPUP | WS_VISIBLE,
        x,
        y,
        sx,
        sy,
        nullptr,
        nullptr,
        _module_handle,
        nullptr
    );
    return !!_hwnd;
}

LRESULT Overlay::_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_PAINT: ValidateRect(hwnd, NULL); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

HRESULT Overlay::_d3d_dcomp_init(int sx, int sy) noexcept {
    const D3D_FEATURE_LEVEL flevel = D3D_FEATURE_LEVEL_11_1;
    HRESULT hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &flevel,
        1,
        D3D11_SDK_VERSION,
        &_d3ddevice,
        NULL,
        &_d3dcontext
    );
    RETURN_HR_ON_FAILURE(hr);

    WRL::ComPtr<IDXGIDevice> dxgidevice = {};
    hr = _d3ddevice.As(&dxgidevice);
    RETURN_HR_ON_FAILURE(hr);
    WRL::ComPtr<IDXGIAdapter> dxgiadapter = {};
    hr = dxgidevice->GetParent(IID_PPV_ARGS(&dxgiadapter));
    RETURN_HR_ON_FAILURE(hr);
    WRL::ComPtr<IDXGIFactory2> dxgifactory2 = {};
    hr = dxgiadapter->GetParent(IID_PPV_ARGS(&dxgifactory2));
    RETURN_HR_ON_FAILURE(hr);

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.Stereo = false;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;
    swapchain_desc.Flags = 0;
    swapchain_desc.BufferCount = 2;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapchain_desc.Width = sx;
    swapchain_desc.Height = sy;
    swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    dxgifactory2->CreateSwapChainForComposition(
        _d3ddevice.Get(), &swapchain_desc, NULL, &_swapchain
    );
    WRL::ComPtr<IDCompositionDevice2> dcomp = {};
    hr = DCompositionCreateDevice2(dxgidevice.Get(), IID_PPV_ARGS(&_dcompdevice));
    RETURN_HR_ON_FAILURE(hr);
    hr = _dcompdevice->CreateTargetForHwnd(_hwnd, true, &_dcomptarget);
    RETURN_HR_ON_FAILURE(hr);
    hr = _dcompdevice->CreateVisual(&_dcompvisual);
    RETURN_HR_ON_FAILURE(hr);
    hr = _dcompvisual->SetContent(_swapchain.Get());
    RETURN_HR_ON_FAILURE(hr);
    hr = _dcomptarget->SetRoot(_dcompvisual.Get());
    RETURN_HR_ON_FAILURE(hr);
    hr = _dcompdevice->Commit();
    return hr;
}

HRESULT Overlay::_create_staging_texture(int sx, int sy) noexcept {
    D3D11_TEXTURE2D_DESC tdesc = {};
    tdesc.SampleDesc.Count = 1;
    tdesc.SampleDesc.Quality = 0;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    tdesc.MiscFlags = 0;
    tdesc.ArraySize = 1;
    tdesc.MipLevels = 1;
    tdesc.Usage = D3D11_USAGE_DYNAMIC;  // CPU Write-only.
    tdesc.Height = sy;
    tdesc.Width = sx;
    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return _d3ddevice->CreateTexture2D(&tdesc, NULL, &_staging);
}
