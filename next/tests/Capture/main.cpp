// Capture sequence adapted from robmikh/Win32CaptureSample (MIT).
#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct CaptureState {
    std::mutex mutex;
    Direct3D11CaptureFrame frame{nullptr};
    winrt::handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};

void SaveTexture(com_ptr<ID3D11Device> const &, com_ptr<ID3D11Texture2D> const &, wchar_t const *);

void Capture(HWND window, com_ptr<ID3D11Device> const &device, wchar_t const *path) {
    auto dxgi = device.as<IDXGIDevice>();
    com_ptr<::IInspectable> inspectable;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
    auto directDevice = inspectable.as<IDirect3DDevice>();
    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    check_hresult(interop->CreateForWindow(window, guid_of<GraphicsCaptureItem>(), put_abi(item)));
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        directDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
    auto session = pool.CreateCaptureSession(item);
    auto state = std::make_shared<CaptureState>();
    if (!state->ready)
        throw std::runtime_error("Cannot create capture synchronization event");
    auto token = pool.FrameArrived(auto_revoke, [state](auto const &sender, auto const &) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->frame) {
            state->frame = sender.TryGetNextFrame();
            if (state->frame)
                SetEvent(state->ready.get());
        }
    });
    std::cout << "capture: starting session" << std::endl;
    session.StartCapture();
    std::cout << "capture: waiting for frame" << std::endl;
    if (WaitForSingleObject(state->ready.get(), 15000) != WAIT_OBJECT_0) {
        token.revoke();
        session.Close();
        pool.Close();
        throw std::runtime_error("No composited capture frame arrived");
    }
    com_ptr<ID3D11Texture2D> texture;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto access =
            state->frame.Surface()
                .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
    }
    token.revoke();
    SaveTexture(device, texture, path);
    session.Close();
    pool.Close();
    std::cout << "capture: frame obtained" << std::endl;
}

void WritePng(wchar_t const *path, UINT width, UINT height, UINT stride, BYTE *pixels) {
    if (static_cast<unsigned long long>(stride) * height > (std::numeric_limits<UINT>::max)()) {
        throw std::runtime_error("Capture is too large");
    }
    auto factory = create_instance<IWICImagingFactory>(CLSID_WICImagingFactory2);
    com_ptr<IWICStream> stream;
    check_hresult(factory->CreateStream(stream.put()));
    check_hresult(stream->InitializeFromFilename(path, GENERIC_WRITE));
    com_ptr<IWICBitmapEncoder> encoder;
    check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    com_ptr<IWICBitmapFrameEncode> frame;
    check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    check_hresult(frame->Initialize(nullptr));
    check_hresult(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    check_hresult(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("Unsupported PNG format");
    check_hresult(frame->WritePixels(height, stride, stride * height, pixels));
    check_hresult(frame->Commit());
    check_hresult(encoder->Commit());
}

void SaveTexture(com_ptr<ID3D11Device> const &device, com_ptr<ID3D11Texture2D> const &texture,
                 wchar_t const *path) {
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if (description.Width > 16384 || description.Height > 16384)
        throw std::runtime_error("Window too large");
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    com_ptr<ID3D11Texture2D> staging;
    check_hresult(device->CreateTexture2D(&description, nullptr, staging.put()));
    com_ptr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.put());
    context->CopyResource(staging.get(), texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
    try {
        WritePng(path, description.Width, description.Height, mapped.RowPitch,
                 static_cast<BYTE *>(mapped.pData));
    } catch (...) {
        context->Unmap(staging.get(), 0);
        throw;
    }
    context->Unmap(staging.get(), 0);
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("Expected HWND, expected PID, PNG path");
        const auto window = reinterpret_cast<HWND>(std::stoull(argv[1], nullptr, 0));
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (!owner || owner != std::stoul(argv[2]))
            throw std::runtime_error("Window ownership mismatch");
        std::cout << "capture: ownership verified" << std::endl;
        init_apartment(apartment_type::multi_threaded);
        std::cout << "capture: COM ready" << std::endl;
        if (!GraphicsCaptureSession::IsSupported())
            throw std::runtime_error("Graphics capture unsupported");
        com_ptr<ID3D11Device> device;
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                        D3D11_SDK_VERSION, device.put(), nullptr, nullptr));
        std::cout << "capture: D3D ready" << std::endl;
        Capture(window, device, argv[3]);
        std::cout << "PASS scoped Windows Graphics Capture\n";
        return 0;
    } catch (hresult_error const &error) {
        std::wcerr << error.message().c_str() << L'\n';
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
