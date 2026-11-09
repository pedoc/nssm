#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <exception>

namespace winrt::NssmNext::implementation {
App::App() {
    InitializeComponent();
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &) {
    m_window = winrt::make<MainWindow>();
    m_window.Closed(
        [](auto const &, auto const &) { Microsoft::UI::Xaml::Application::Current().Exit(); });
    m_window.Activate();
    m_window.AppWindow().Resize({1160, 760});
}
} // namespace winrt::NssmNext::implementation

int RunWinUi() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto &&) { winrt::make<winrt::NssmNext::implementation::App>(); });
        return 0;
    } catch (winrt::hresult_error const &error) {
        MessageBoxW(nullptr, error.message().c_str(), L"NSSM Next prototype", MB_OK | MB_ICONERROR);
        return 1;
    } catch (std::exception const &error) {
        OutputDebugStringA(error.what());
        MessageBoxW(nullptr, L"UI initialization failed. No service configuration was changed.",
                    L"NSSM Next prototype", MB_OK | MB_ICONERROR);
        return 1;
    }
}
