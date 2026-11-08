#pragma once
#include "pch.h"
#include "../Core/ServiceModel.h"
#include <memory>

namespace nssm_next::ui {
struct ServiceEditor {
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
    winrt::Microsoft::UI::Xaml::Controls::TextBox name;
    winrt::Microsoft::UI::Xaml::Controls::TextBox displayName;
    winrt::Microsoft::UI::Xaml::Controls::TextBox application;
    winrt::Microsoft::UI::Xaml::Controls::TextBox arguments;
    winrt::Microsoft::UI::Xaml::Controls::TextBox directory;
    winrt::Microsoft::UI::Xaml::Controls::TextBox description;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox startup;
    winrt::Microsoft::UI::Xaml::Controls::InfoBar error;
    bool saved = false;
    ServiceInfo saved_service;
    ServiceConfiguration Configuration() const;
    void ShowError(const std::wstring &message);
};
std::shared_ptr<ServiceEditor> MakeServiceEditor(const ServiceConfiguration &configuration,
                                                 bool isNew,
                                                 winrt::Microsoft::UI::Xaml::XamlRoot const &root,
                                                 HWND owner, const std::wstring &account);
bool RelaunchElevated(HWND owner);
std::wstring TestApplicationPath();
} // namespace nssm_next::ui
