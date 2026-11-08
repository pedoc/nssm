#pragma once
#include "MainWindow.g.h"
#include "../Core/NativeServices.h"

namespace winrt::NssmNext::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void Root_Loaded(Windows::Foundation::IInspectable const &,
                     Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Refresh_Click(Windows::Foundation::IInspectable const &,
                       Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Search_Changed(Windows::Foundation::IInspectable const &,
                        Microsoft::UI::Xaml::Controls::TextChangedEventArgs const &);
    void Selection_Changed(Windows::Foundation::IInspectable const &,
                           Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const &);

  private:
    nssm_next::ServiceSnapshot m_snapshot;
    bool m_loaded = false;
    winrt::fire_and_forget Refresh();
    void RenderServices();
};
} // namespace winrt::NssmNext::implementation

namespace winrt::NssmNext::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
} // namespace winrt::NssmNext::factory_implementation
