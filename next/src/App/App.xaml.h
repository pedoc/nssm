#pragma once
#include "App.xaml.g.h"

namespace winrt::NssmNext::implementation {
struct App : AppT<App> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &);

  private:
    Microsoft::UI::Xaml::Window m_window{nullptr};
};
} // namespace winrt::NssmNext::implementation
