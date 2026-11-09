#pragma once
#include "../pch.h"
#include "../../Core/ServiceConfigurationCodec.h"
#include <array>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace nssm_next::ui::editor_pages {
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
enum class PageId : std::size_t {
    Application, Details, Logon, Dependencies, Process, Shutdown, Exit, Io,
    Rotation, Environment, Hooks, Count
};
constexpr std::size_t PageCount = static_cast<std::size_t>(PageId::Count);
const wchar_t *PageKey(PageId page);
const wchar_t *PageTitle(PageId page);
std::size_t FindPage(const std::wstring &key);

struct ValidationError : ServiceError {
    ValidationError(PageId page, std::wstring field, std::wstring message)
        : ServiceError(std::move(message)), page(page), field(std::move(field)) {}
    PageId page;
    std::wstring field;
};
struct Page {
    controls::ScrollViewer scroll{nullptr};
    controls::StackPanel body{nullptr};
    xaml::FrameworkElement first{nullptr};
};

std::wstring Text(winrt::hstring const &value);
bool Checked(controls::CheckBox const &box);
void Id(xaml::DependencyObject const &object, const std::wstring &id);
controls::TextBlock Note(const std::wstring &text);
void Help(xaml::FrameworkElement const &control, const std::wstring &text);
controls::TextBox TextField(const wchar_t *label, const wchar_t *id,
                            const std::wstring &value, int limit, bool multiline = false);
controls::CheckBox Check(const wchar_t *label, const wchar_t *id, bool value);
controls::ComboBox Choice(const wchar_t *label, const wchar_t *id,
                          std::initializer_list<const wchar_t *> options, int selected);
// Labels live beside native controls, as in the original NSSM dialog. Keep the
// control itself (and its automation ID) intact when composing a form row.
controls::Grid FieldRow(xaml::FrameworkElement const &field,
                        xaml::UIElement const &content = nullptr, double labelWidth = 144);
controls::Grid WithButton(controls::TextBox const &field, controls::Button const &button);
controls::Grid Pair(xaml::UIElement const &left, xaml::UIElement const &right);
controls::StackPanel Group(controls::StackPanel const &body, const wchar_t *title);
void Append(controls::StackPanel const &body, xaml::UIElement const &element);
void ValidateText(const std::wstring &value, std::size_t maximum, PageId page,
                  const wchar_t *id, const wchar_t *label, bool required = false);
std::uint32_t Unsigned32(controls::TextBox const &field, PageId page, const wchar_t *label);
std::uint64_t Unsigned64(controls::TextBox const &field, PageId page, const wchar_t *label);
bool SameAccount(const std::wstring &left, const std::wstring &right);
enum class PickerKind { Application, Directory, Input, Output };
std::wstring PickPath(HWND owner, PickerKind kind);

// Every form is constructed once. Event handlers hold weak references; only this
// object owns the draft controls. The baseline contains no passwords.
struct EditorPages : std::enable_shared_from_this<EditorPages> {
    void Initialize(const ServiceConfiguration &configuration, bool isNew, HWND owner);
    std::array<Page, PageCount> pages;
    std::function<void()> changed;
    std::function<void(const std::wstring &, const std::wstring &)> show_error;

    ServiceConfiguration Read() const;
    CredentialRequest TakeCredentials();
    void ValidateCredentials() const;
    void ClearSecrets();
    bool IsDirty() const;
    void FocusField(const std::wstring &id);
    std::wstring DraftAccount() const;
    bool SystemAccount() const;
    // Hosted in the shared footer, but still watched, validated and owned here.
    controls::TextBox ServiceNameField() const { return name; }

  private:
    void BuildApplication();
    void UpdateApplicationDefaults();
    void BuildDetails();
    void BuildLogon();
    void BuildDependencies();
    void BuildProcess();
    void BuildShutdown();
    void BuildExit();
    void BuildIo();
    void BuildRotation();
    void BuildEnvironment();
    void BuildHooks();
    void ReadBasic(ServiceConfiguration &) const;
    void ReadLoginDependencies(ServiceConfiguration &) const;
    void ReadProcess(ServiceConfiguration &) const;
    void ReadOutput(ServiceConfiguration &) const;
    void ReadEnvironmentHooks(ServiceConfiguration &) const;
    void UpdateEnabled();
    void OnChanged();
    void Watch(controls::TextBox const &control);
    void Watch(controls::CheckBox const &control);
    void Watch(controls::ComboBox const &control);
    void Watch(controls::PasswordBox const &control);
    void Register(xaml::FrameworkElement const &control);
    void Browse(controls::Button const &button, controls::TextBox const &target,
                PickerKind kind, PageId page, bool quote = false);
    void AddDependency();
    void RemoveDependency();
    void RefreshDependencies();
    void SelectHookEvent();
    void SelectHookAction();
    void RevealHook(std::size_t index);
    std::size_t CurrentHook() const;

    ServiceConfiguration original;
    bool is_new = false, initialized = false, selecting_hook = false;
    bool credentials_extracted = false;
    bool suggest_name = false, suggest_directory = false;
    std::wstring suggested_name, suggested_directory;
    HWND owner = nullptr;
    std::vector<std::function<std::wstring()>> snapshots;
    std::vector<std::wstring> baseline;
    std::map<std::wstring, xaml::FrameworkElement> focus_targets;

    controls::TextBox name{nullptr}, display_name{nullptr}, application{nullptr};
    controls::TextBox arguments{nullptr}, directory{nullptr}, description{nullptr};
    controls::ComboBox startup{nullptr}, account_kind{nullptr};
    controls::TextBox username{nullptr};
    controls::PasswordBox password{nullptr}, confirmation{nullptr};
    controls::CheckBox grant_logon{nullptr};
    controls::TextBlock password_notice{nullptr};
    controls::TextBox dependency_entry{nullptr};
    controls::CheckBox dependency_group{nullptr};
    controls::ListView dependency_list{nullptr};
    controls::Button dependency_remove{nullptr};
    std::vector<std::wstring> dependency_draft;

    controls::ComboBox priority{nullptr};
    std::vector<std::uint32_t> priority_values;
    controls::CheckBox all_cpus{nullptr}, console{nullptr};
    controls::TextBox affinity{nullptr};
    controls::CheckBox stop_console{nullptr}, stop_window{nullptr};
    controls::CheckBox stop_threads{nullptr}, stop_terminate{nullptr}, kill_tree{nullptr};
    controls::TextBox console_ms{nullptr}, window_ms{nullptr}, threads_ms{nullptr};
    controls::ComboBox exit_action{nullptr};
    controls::TextBox restart_ms{nullptr}, throttle_ms{nullptr};

    std::array<controls::TextBox, 3> streams{{nullptr, nullptr, nullptr}};
    controls::CheckBox timestamp{nullptr}, rotate{nullptr}, online{nullptr};
    controls::TextBox older_than{nullptr}, larger_than{nullptr}, rotation_delay{nullptr};
    controls::ComboBox stdout_disposition{nullptr}, stderr_disposition{nullptr};
    controls::CheckBox replacement_enabled{nullptr}, extra_enabled{nullptr};
    controls::TextBox replacement{nullptr}, extra{nullptr};
    std::wstring original_replacement_text, original_extra_text;

    controls::ComboBox hook_event{nullptr}, hook_action{nullptr};
    std::array<controls::TextBox, 8> hook_commands{{nullptr, nullptr, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr}};
    controls::CheckBox redirect_hooks{nullptr};
    controls::Grid hook_host{nullptr};
    std::array<int, 5> hook_action_drafts{};
};
} // namespace nssm_next::ui::editor_pages
