#pragma once
#include "pch.h"
#include "../Core/ServiceModel.h"
#include <functional>
#include <memory>

namespace nssm_next::ui {
namespace editor_pages { struct EditorPages; }

// UI-thread object. The caller owns persistence and must keep the parent alive until
// ShowAsync completes. No service manager, registry or account operations live here.
struct ServiceEditor : std::enable_shared_from_this<ServiceEditor> {
    ~ServiceEditor();
    bool saved = false;
    ServiceInfo saved_service;
    // Prefer a synchronous, weak-capturing launcher for a named async operation;
    // that operation should own its state/credentials across suspension.
    std::function<void()> save_requested;

    ServiceConfiguration Configuration() const;
    CredentialRequest TakeCredentials();
    void ShowError(const std::wstring &message, const std::wstring &page = {});
    void SetSaving(bool saving);
    bool IsSaving() const noexcept { return m_saving; }
    bool IsDirty() const;
    bool IsOpen() const noexcept { return !m_closed; }
    winrt::Windows::Foundation::IAsyncAction ShowAsync();
    void CloseSaved();
    void RequestClose();
    void Activate();

  private:
    ServiceEditor() = default;
    void Initialize(const ServiceConfiguration &, bool,
                    winrt::Microsoft::UI::Xaml::XamlRoot const &, HWND, const std::wstring &);
    void SelectPage(std::size_t index);
    void UpdateStatus();
    void Save();
    static winrt::fire_and_forget ConfirmDiscard(std::shared_ptr<ServiceEditor> state);

    friend std::shared_ptr<ServiceEditor> MakeServiceEditor(
        const ServiceConfiguration &, bool, winrt::Microsoft::UI::Xaml::XamlRoot const &,
        HWND, const std::wstring &);
    std::shared_ptr<editor_pages::EditorPages> m_pages;
    winrt::Microsoft::UI::Xaml::Window m_window{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Grid m_workspace{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ContentControl m_workspaceHost{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::SelectorBar m_navigation{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::InfoBar m_error{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_accountNotice{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_status{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_save{nullptr}, m_cancel{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ProgressRing m_progress{nullptr};
    winrt::handle m_closedEvent;
    HWND m_hwnd = nullptr;
    bool m_isNew = false, m_saving = false, m_closed = false;
    bool m_shown = false, m_allowClose = false, m_confirming = false;
};

std::shared_ptr<ServiceEditor> MakeServiceEditor(
    const ServiceConfiguration &configuration, bool isNew,
    winrt::Microsoft::UI::Xaml::XamlRoot const &parentRoot,
    HWND owner, const std::wstring &currentAccount);
bool RelaunchElevated(HWND owner);
std::wstring TestApplicationPath();
} // namespace nssm_next::ui
