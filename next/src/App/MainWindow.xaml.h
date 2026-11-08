#pragma once
#include "MainWindow.g.h"
#include "../Core/NativeServices.h"
#include <optional>

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
    void New_Click(Windows::Foundation::IInspectable const &,
                   Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Edit_Click(Windows::Foundation::IInspectable const &,
                    Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Delete_Click(Windows::Foundation::IInspectable const &,
                      Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Start_Click(Windows::Foundation::IInspectable const &,
                     Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Stop_Click(Windows::Foundation::IInspectable const &,
                    Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Restart_Click(Windows::Foundation::IInspectable const &,
                       Microsoft::UI::Xaml::RoutedEventArgs const &);
    void Elevate_Click(Windows::Foundation::IInspectable const &,
                       Microsoft::UI::Xaml::RoutedEventArgs const &);

  private:
    enum class Action {
        Start,
        Stop,
        Restart,
        Remove
    };
    std::shared_ptr<nssm_next::IServiceManager> m_manager;
    nssm_next::ServiceSnapshot m_snapshot;
    std::wstring m_selected;
    HWND m_owner = nullptr;
    bool m_loaded = false;
    bool m_busy = false;
    bool m_dialog_open = false;
    bool m_mutating = false;
    bool m_closed = false;
    winrt::fire_and_forget Refresh(std::wstring message = {});
    winrt::fire_and_forget ShowEditor(bool isNew);
    winrt::fire_and_forget Execute(Action action);
    winrt::fire_and_forget Elevate();
    Windows::Foundation::IAsyncOperation<bool> EnsureWriteAccess();
    std::optional<nssm_next::ServiceInfo> Selected();
    void RenderServices();
    void RenderDetails();
    void UpdateControls();
    void SetBusy(bool busy, bool mutating = false);
    void ShowError(const std::wstring &message);
};
} // namespace winrt::NssmNext::implementation
namespace winrt::NssmNext::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
} // namespace winrt::NssmNext::factory_implementation
