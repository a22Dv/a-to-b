/**
 * @brief
 * Custom DXGI Desktop
 * Duplication implementation.
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
 *   Expect the delay for each call to reach ~5ms at ~90% memory usage,
 *   and beyond that if page-swapping occurs. (>20ms)
 * - Average-case capture performance, ~2ms from GPU to return to caller.
 * - Tested under: (Windows 11, Ryzen 7 8845HS, LPDDR5X-7500, 1080P60).
 */

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>

#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <winerror.h>
#include <winuser.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace WRL = Microsoft::WRL;

#define DLL_EXPORT __declspec(dllexport)
#define RETURN_ON_HR_FAILURE(hr) \
    do {                         \
        if (FAILED(hr)) {        \
            return hr;           \
        }                        \
    } while (false)
#define RETURN_ON_HR_FAILURE_ACTION(hr, action) \
    do {                                        \
        if (FAILED(hr)) {                       \
            action;                             \
            return hr;                          \
        }                                       \
    } while (false)

struct ScreenCapture;

extern "C" {

/**
 * @brief
 * Primary entry point. Creates the object on memory
 * and provides a handle to the caller.
 */
DLL_EXPORT HRESULT create_screen_capture_object(ScreenCapture **out) noexcept;

/**
 * @brief
 * Capture current frame on the current buffer.
 *
 * @note
 * Data must be equal to the size of the monitor's resolution.
 * It must be formatted as B8G8R8A8.Behavior is undefined otherwise.
 */
DLL_EXPORT HRESULT capture_frame(ScreenCapture *obj, void *data, int dx, int dy, int dz) noexcept;

/**
 * @brief
 * Destroy screen capture object. Does nothing if a nullptr is passed.
 */
DLL_EXPORT void destroy_screen_capture_object(ScreenCapture **objptr) noexcept;
}

class ScreenCapture {
   public:
    ScreenCapture() = default;

    /**
     * @brief Primary initializer.
     */
    HRESULT initialize();

    /**
     * @brief
     * Capture frame call. Copies duplicated output
     * onto data out-parameter. Caller must provide size of
     * data pointer in width and height and must ensure
     * data is contiguous in memory.
     */
    HRESULT capture_frame(void *data, int dx, int dy, int dz);

   private:
    WRL::ComPtr<ID3D11Device> _d3ddevice = {};
    WRL::ComPtr<ID3D11DeviceContext> _d3dcontext = {};
    WRL::ComPtr<IDXGIOutputDuplication> _dxgidupl = {};
    WRL::ComPtr<ID3D11Texture2D> _d3dstaging = {};
    int _display_width = 0;
    int _display_height = 0;

    /**
     * @brief
     * Sets up a staging texture from a given device.
     */
    HRESULT _create_staging_texture();
    /**
     * @brief
     * Sets up a Duplication device
     * based on a given D3D11 Device.
     */
    HRESULT _create_duplication_device();

    /**
     * @brief
     * Usually the first call of the D3D11 initalization sequence.
     * Sets up the D3D11 Device and Context.
     */
    HRESULT _create_device_and_ctx();
};

HRESULT ScreenCapture::initialize() {
    HRESULT hr = _create_device_and_ctx();
    RETURN_ON_HR_FAILURE(hr);
    hr = _create_duplication_device();
    RETURN_ON_HR_FAILURE(hr);
    _display_width = GetSystemMetrics(SM_CXSCREEN);
    _display_height = GetSystemMetrics(SM_CYSCREEN);
    hr = _create_staging_texture();
    return hr;
}

DLL_EXPORT HRESULT capture_frame(ScreenCapture *obj, void *data, int dx, int dy, int dz) noexcept {
    if (!(obj && data)) {
        return E_POINTER;
    }
    try {
        HRESULT hr = obj->capture_frame(data, dx, dy, dz);
        if (FAILED(hr)) {
            return hr;
        }
    } catch (...) {
        return E_FAIL;
    }
    return S_OK;
}

HRESULT ScreenCapture::_create_staging_texture() {
    D3D11_TEXTURE2D_DESC tdesc = {};
    tdesc.SampleDesc.Count = 1;
    tdesc.SampleDesc.Quality = 0;
    tdesc.ArraySize = 1;
    tdesc.BindFlags = 0;
    tdesc.MiscFlags = 0;
    tdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tdesc.MipLevels = 1;
    tdesc.Height = GetSystemMetrics(SM_CYSCREEN);
    tdesc.Width = GetSystemMetrics(SM_CXSCREEN);
    tdesc.Usage = D3D11_USAGE_STAGING;
    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return _d3ddevice->CreateTexture2D(&tdesc, NULL, &_d3dstaging);
}

HRESULT ScreenCapture::_create_duplication_device() {
    HRESULT hr = S_OK;
    WRL::ComPtr<IDXGIDevice> dxgidevice = {};
    hr = _d3ddevice.As(&dxgidevice);
    RETURN_ON_HR_FAILURE(hr);

    WRL::ComPtr<IDXGIAdapter> dxgiadapter = {};
    hr = dxgidevice->GetParent(__uuidof(IDXGIAdapter), &dxgiadapter);
    RETURN_ON_HR_FAILURE(hr);

    WRL::ComPtr<IDXGIOutput> dxgioutput = {};
    hr = dxgiadapter->EnumOutputs(0, &dxgioutput);
    RETURN_ON_HR_FAILURE(hr);

    WRL::ComPtr<IDXGIOutput1> dxgioutput1 = {};
    hr = dxgioutput.As(&dxgioutput1);
    RETURN_ON_HR_FAILURE(hr);

    return dxgioutput1->DuplicateOutput(_d3ddevice.Get(), &_dxgidupl);
}

HRESULT ScreenCapture::_create_device_and_ctx() {
#ifndef NDEBUG
    constexpr UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;
#else
    constexpr UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#endif
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_1;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flag,
        &level,
        1,
        D3D11_SDK_VERSION,
        &_d3ddevice,
        NULL,
        &_d3dcontext
    );
    return hr;
}

HRESULT ScreenCapture::capture_frame(void *data, int dx, int dy, int dz) {
    constexpr UINT timeout = UINT(1000.0 / 60.0);
    constexpr UINT channel_count = 4;  // BGRA.
    if (!data) {
        return E_POINTER;
    }
    const UINT tframe_size = _display_height * _display_width * channel_count;
    if (!(dy == _display_height && dx == _display_width && dz == channel_count)) {
        return E_INVALIDARG;
    }
    HRESULT hr = S_OK;
    DXGI_OUTDUPL_FRAME_INFO frame_info = {};
    WRL::ComPtr<IDXGIResource> resource = {};
    hr = _dxgidupl->AcquireNextFrame(timeout, &frame_info, &resource);
    RETURN_ON_HR_FAILURE(hr);

    WRL::ComPtr<ID3D11Texture2D> resource_texture = {};
    hr = resource.As(&resource_texture);
    RETURN_ON_HR_FAILURE_ACTION(hr, _dxgidupl->ReleaseFrame());

    _d3dcontext->CopyResource(_d3dstaging.Get(), resource_texture.Get());
    D3D11_MAPPED_SUBRESOURCE subresource = {};
    hr = _d3dcontext->Map(_d3dstaging.Get(), 0, D3D11_MAP_READ, 0, &subresource);
    RETURN_ON_HR_FAILURE_ACTION(hr, _dxgidupl->ReleaseFrame());

    if (subresource.RowPitch == dx * channel_count) {
        std::memcpy(data, subresource.pData, tframe_size);
    } else {
        std::uint8_t *__restrict src = static_cast<std::uint8_t *>(subresource.pData);
        std::uint8_t *__restrict dst = static_cast<std::uint8_t *>(data);
        const std::size_t src_stride = subresource.RowPitch;
        const std::size_t dst_stride = dx * channel_count;
        for (int i = 0; i < dy; ++i) {
            std::memcpy(dst, src, dst_stride);
            src += src_stride;
            dst += dst_stride;
        }
    }
    _d3dcontext->Unmap(_d3dstaging.Get(), 0);
    hr = _dxgidupl->ReleaseFrame();
    return hr;
}

DLL_EXPORT HRESULT create_screen_capture_object(ScreenCapture **out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    HRESULT hr = S_OK;
    try {
        auto *scrobj = new ScreenCapture();
        hr = scrobj->initialize();
        RETURN_ON_HR_FAILURE(hr);
        *out = scrobj;
    } catch (...) {
        if (FAILED(hr)) {
            return hr;
        } else {
            return E_FAIL;
        }
    }
    return hr;
}

DLL_EXPORT void destroy_screen_capture_object(ScreenCapture **objptr) noexcept {
    if (!objptr) {
        return;
    }
    delete *objptr;
    *objptr = nullptr;
}