#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgiformat.h>
#include <stdint.h>
#include <string.h>
#include <winerror.h>

#define DLLEXPORT __declspec(dllexport)
#define ACQUIRE_TIMEOUT_MS 15
#define DX_CALL(object, function, ...) (((object)->lpVtbl)->function(object, __VA_ARGS__))
#define REQUIRE(cond, action) \
    do {                      \
        if (!(cond)) {        \
            action;           \
        }                     \
    } while (0)

typedef struct {
    // Staging texture is required as the raw frame
    // cannot be accessed by the CPU by itself.
    ID3D11Texture2D *d3d11staging;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    int height;
    int width;
} FrameData;

typedef struct {
    ID3D11Device *d3d11dev;
    ID3D11DeviceContext *d3d11ctx;
    IDXGIDevice1 *dxdev;
    IDXGIAdapter1 *dxadpt;
    IDXGIOutput *dxout;
    IDXGIOutput1 *dxout1;
    IDXGIOutputDuplication *dxdupl;
} DXGIState;

typedef struct {
    DXGIState dxstate;
    FrameData fdata;
} ScreenCapture;

HRESULT dxgi_init(DXGIState *state);
HRESULT fdata_init(FrameData *state, IDXGIOutput1 *out, ID3D11Device *dev);

/// @brief Create ScreenCapture object.
DLLEXPORT ScreenCapture *capture_state_create() {
    ScreenCapture *state = calloc(1, sizeof(ScreenCapture));
    REQUIRE(state, goto exit);
    REQUIRE(SUCCEEDED(dxgi_init(&state->dxstate)), goto exit);
    REQUIRE(
        SUCCEEDED(fdata_init(&state->fdata, state->dxstate.dxout1, state->dxstate.d3d11dev)),
        goto exit
    );
    return state;
exit:
    return NULL;
}

/// @brief Destroy ScreenCapture object.
DLLEXPORT void capture_state_destroy(ScreenCapture *state) {
    DXGIState *dxstate = &state->dxstate;
    FrameData *fdata = &state->fdata;
    DX_CALL(fdata->d3d11staging, Release);
    DX_CALL(dxstate->dxdupl, Release);
    DX_CALL(dxstate->dxout1, Release);
    DX_CALL(dxstate->dxout, Release);
    DX_CALL(dxstate->dxadpt, Release);
    DX_CALL(dxstate->dxdev, Release);
    DX_CALL(dxstate->d3d11ctx, Release);
    DX_CALL(dxstate->d3d11dev, Release);
}

/// @brief Accessor for capture state height.
DLLEXPORT int capture_state_height(ScreenCapture *state) {
    return state->fdata.height;
}

/// @brief Accessor for capture state width.
DLLEXPORT int capture_state_width(ScreenCapture *state) {
    return state->fdata.width;
}

/// @brief Helper initialization function. Initialize frame data.
HRESULT fdata_init(FrameData *state, IDXGIOutput1 *out, ID3D11Device *dev) {
    DXGI_OUTPUT_DESC desc;
    HRESULT hr = DX_CALL(out, GetDesc, &desc);
    REQUIRE(SUCCEEDED(hr), goto exit);

    D3D11_TEXTURE2D_DESC d3ddsc = {};
    d3ddsc.Height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    d3ddsc.Width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    d3ddsc.Usage = D3D11_USAGE_STAGING;
    d3ddsc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d3ddsc.MipLevels = 1;
    d3ddsc.ArraySize = 1;
    d3ddsc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d3ddsc.SampleDesc.Count = 1;

    state->height = d3ddsc.Height;
    state->width = d3ddsc.Width;

    hr = DX_CALL(dev, CreateTexture2D, &d3ddsc, NULL, &state->d3d11staging);
    REQUIRE(SUCCEEDED(hr), goto exit);
exit:
    return hr;
};

/// @brief Initialize DXGI state object.
HRESULT dxgi_init(DXGIState *state) {
    const D3D_FEATURE_LEVEL feat_lvl = D3D_FEATURE_LEVEL_11_0;
    uint8_t prog = 0;
    HRESULT hr = S_OK;
    hr = D3D11CreateDevice( // Device create call.
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &feat_lvl,
        1,
        D3D11_SDK_VERSION,
        &state->d3d11dev,
        NULL,
        &state->d3d11ctx
    );
    REQUIRE(SUCCEEDED(hr), goto exit);

    // Call chain due to hierarchy. *1 is used as it is the version to support duplication.
    prog = 1;
    hr = DX_CALL(state->d3d11dev, QueryInterface, &IID_IDXGIDevice1, (void **)&state->dxdev);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 2;
    hr = DX_CALL(state->dxdev, GetParent, &IID_IDXGIAdapter1, (void **)&state->dxadpt);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 3;
    hr = DX_CALL(state->dxadpt, EnumOutputs, 0, &state->dxout);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 4;
    hr = DX_CALL(state->dxout, QueryInterface, &IID_IDXGIOutput1, (void **)&state->dxout1);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 5;
    hr = DX_CALL(state->dxout1, DuplicateOutput, (IUnknown *)state->d3d11dev, &state->dxdupl);
    REQUIRE(SUCCEEDED(hr), goto exit);
    return hr;
exit:
    switch (prog - 1) {
        case 5: DX_CALL(state->dxdupl, Release);
        case 4: DX_CALL(state->dxout1, Release);
        case 3: DX_CALL(state->dxout, Release);
        case 2: DX_CALL(state->dxadpt, Release);
        case 1: DX_CALL(state->dxdev, Release);
        case 0: DX_CALL(state->d3d11ctx, Release); DX_CALL(state->d3d11dev, Release);
        default: break;
    }
    return hr;
}

/// @brief Retrieve frame and copy to given memory location.
DLLEXPORT HRESULT capture_state_get_frame(ScreenCapture *state, void *frame) {
    HRESULT hr = S_OK;
    uint8_t prog = 0;
    REQUIRE(state, hr = E_FAIL; goto exit);

    prog = 1;
    IDXGIResource *rs = NULL;
    DXGI_OUTDUPL_FRAME_INFO dpinfo = {};
    hr = DX_CALL(state->dxstate.dxdupl, AcquireNextFrame, ACQUIRE_TIMEOUT_MS, &dpinfo, &rs);
    REQUIRE(SUCCEEDED(hr), goto exit);

    prog = 2;
    ID3D11Texture2D *data = NULL;
    hr = DX_CALL(rs, QueryInterface, &IID_ID3D11Texture2D, (void **)&data);
    REQUIRE(SUCCEEDED(hr), goto exit);
    DX_CALL(rs, Release);
    rs = NULL;
    state->fdata.frame_info = dpinfo;
    DX_CALL(
        state->dxstate.d3d11ctx,
        CopyResource,
        (ID3D11Resource *)state->fdata.d3d11staging,
        (ID3D11Resource *)data
    );

    D3D11_MAPPED_SUBRESOURCE sbrsc;
    hr = DX_CALL(
        state->dxstate.d3d11ctx,
        Map,
        (ID3D11Resource *)state->fdata.d3d11staging,
        0,
        D3D11_MAP_READ,
        0,
        &sbrsc
    );
    REQUIRE(SUCCEEDED(hr), goto exit);

    char *rframe = sbrsc.pData;
    char *cframe = frame;

    // Switch over to a single contiguous memcpy call if GPU padding does not exist.
    if (sbrsc.RowPitch == state->fdata.width * sizeof(uint32_t)) {
        memcpy(cframe, rframe, state->fdata.width * sizeof(uint32_t) * state->fdata.height);
    } else {
        for (int i = 0; i < state->fdata.height; ++i) {
            const int dstride = state->fdata.width * sizeof(uint32_t);
            memcpy(cframe, rframe, dstride);  // BGRA, 8-bit 4 channels.
            rframe += sbrsc.RowPitch;
            cframe += dstride;
        }
    }

    DX_CALL(state->dxstate.d3d11ctx, Unmap, (ID3D11Resource *)state->fdata.d3d11staging, 0);
    DX_CALL(state->dxstate.dxdupl, ReleaseFrame);
    return hr;
exit:
    switch (prog - 1) {
        case 2:
        case 1: DX_CALL(rs, Release);
        default: break;
    }
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        hr = S_OK;  // Override timeout.
    }
    return hr;
}