#include "pch.h"
#include "EditorPages.h"
#include <shobjidl.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Text.h>
#include <algorithm>
#include <limits>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace nssm_next::ui::editor_pages {
namespace {
constexpr std::array<const wchar_t *, PageCount> Keys{
    L"Application", L"Details", L"Logon", L"Dependencies", L"Process", L"Shutdown",
    L"Exit", L"IO", L"Rotation", L"Environment", L"Hooks"};
constexpr std::array<const wchar_t *, PageCount> Titles{
    L"应用程序", L"详情", L"登录", L"依赖", L"进程", L"关闭",
    L"退出", L"I/O", L"日志轮转", L"环境变量", L"钩子"};
std::wstring ControlId(DependencyObject const &control) {
    return Text(Automation::AutomationProperties::GetAutomationId(control));
}
std::uint64_t Number(TextBox const &field, std::uint64_t maximum, PageId page,
                     const wchar_t *label) {
    const auto id = ControlId(field);
    try {
        const auto text = Text(field.Text());
        ValidateText(text, 20, page, id.c_str(), label, true);
        return ParseUnsigned(text, maximum, label);
    } catch (ServiceError const &error) {
        throw ValidationError(page, id, error.message +
            (field.IsEnabled() ? std::wstring{} : L" 请先启用对应选项以修正该输入。"));
    }
}
} // namespace

const wchar_t *PageKey(PageId page) { return Keys.at(static_cast<std::size_t>(page)); }
const wchar_t *PageTitle(PageId page) { return Titles.at(static_cast<std::size_t>(page)); }
std::size_t FindPage(const std::wstring &key) {
    for (std::size_t i = 0; i < PageCount; ++i)
        if (SameAccount(key, Keys[i]) || key == Titles[i] ||
            (i == static_cast<std::size_t>(PageId::Io) && key == L"I/O"))
            return i;
    return PageCount;
}
std::wstring Text(hstring const &value) { return {value.c_str(), value.size()}; }
bool Checked(CheckBox const &box) {
    const auto value = box.IsChecked();
    return value && value.Value();
}
void Id(DependencyObject const &object, const std::wstring &id) {
    Automation::AutomationProperties::SetAutomationId(object, id);
}
TextBlock Note(const std::wstring &text) {
    TextBlock block;
    block.Text(text);
    block.TextWrapping(TextWrapping::Wrap);
    block.FontSize(12);
    block.IsTextSelectionEnabled(true);
    return block;
}
void Help(FrameworkElement const &control, const std::wstring &text) {
    auto description = Note(text);
    description.MaxWidth(360);
    ToolTip tooltip;
    tooltip.Content(description);
    ToolTipService::SetToolTip(control, tooltip);
    Automation::AutomationProperties::SetHelpText(control, text);
}
TextBox TextField(const wchar_t *label, const wchar_t *id, const std::wstring &value,
                  int limit, bool multiline) {
    TextBox field;
    field.Header(box_value(label));
    // Loading an unsupported pre-existing value must never truncate it. Native
    // length errors are reported during validation, not silently fixed on load.
    field.MaxLength((std::max)(limit, static_cast<int>(value.size())));
    field.Text(hstring(value.data(), static_cast<std::uint32_t>(value.size())));
    field.HorizontalAlignment(HorizontalAlignment::Stretch);
    field.IsSpellCheckEnabled(false);
    field.IsTextPredictionEnabled(false);
    Id(field, id);
    if (multiline) {
        field.AcceptsReturn(true);
        field.TextWrapping(TextWrapping::Wrap);
        field.MinHeight(96);
        field.MaxHeight(240);
        ScrollViewer::SetVerticalScrollBarVisibility(field, ScrollBarVisibility::Auto);
    }
    return field;
}
CheckBox Check(const wchar_t *label, const wchar_t *id, bool value) {
    CheckBox field;
    field.Content(box_value(label));
    field.IsChecked(value);
    Id(field, id);
    return field;
}
ComboBox Choice(const wchar_t *label, const wchar_t *id,
                std::initializer_list<const wchar_t *> options, int selected) {
    ComboBox field;
    field.Header(box_value(label));
    field.HorizontalAlignment(HorizontalAlignment::Stretch);
    Id(field, id);
    for (const auto *option : options)
        field.Items().Append(box_value(option));
    field.SelectedIndex(selected);
    return field;
}
Grid FieldRow(FrameworkElement const &field, UIElement const &content, double labelWidth) {
    // Move, rather than duplicate, the native Header into a shared label column.
    // LabeledBy preserves its accessible relationship after removing Header.
    Windows::Foundation::IInspectable header{nullptr};
    if (const auto text = field.try_as<TextBox>()) {
        header = text.Header(); text.Header(nullptr);
    } else if (const auto choice = field.try_as<ComboBox>()) {
        header = choice.Header(); choice.Header(nullptr);
    } else if (const auto secret = field.try_as<PasswordBox>()) {
        header = secret.Header(); secret.Header(nullptr);
    }
    TextBlock label;
    label.Text(header ? unbox_value<hstring>(header) : hstring{});
    label.TextWrapping(TextWrapping::Wrap);
    label.Margin(Thickness{0, 6, 0, 0});
    Automation::AutomationProperties::SetLabeledBy(field, label);
    Grid row;
    ColumnDefinition caption, value;
    caption.Width(GridLengthHelper::FromValueAndType(labelWidth, GridUnitType::Pixel));
    value.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    row.ColumnDefinitions().Append(caption);
    row.ColumnDefinitions().Append(value);
    row.ColumnSpacing(12);
    row.Children().Append(label);
    const auto editor = content ? content : field.as<UIElement>();
    Grid::SetColumn(editor.as<FrameworkElement>(), 1);
    row.Children().Append(editor);
    return row;
}
Grid WithButton(TextBox const &field, Button const &button) {
    Grid row;
    ColumnDefinition fill, fixed;
    fill.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    fixed.Width(GridLengthHelper::Auto());
    row.ColumnDefinitions().Append(fill);
    row.ColumnDefinitions().Append(fixed);
    row.ColumnSpacing(8);
    row.Children().Append(field);
    button.VerticalAlignment(VerticalAlignment::Top);
    Grid::SetColumn(button, 1);
    row.Children().Append(button);
    return FieldRow(field, row);
}
Grid Pair(UIElement const &left, UIElement const &right) {
    Grid row;
    for (int i = 0; i < 2; ++i) {
        ColumnDefinition column;
        column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        row.ColumnDefinitions().Append(column);
    }
    row.ColumnSpacing(20);
    row.Children().Append(left);
    Grid::SetColumn(right.as<FrameworkElement>(), 1);
    row.Children().Append(right);
    return row;
}
StackPanel Group(StackPanel const &body, const wchar_t *title) {
    StackPanel group;
    group.Spacing(8);
    TextBlock heading;
    heading.Text(title);
    heading.FontWeight(Windows::UI::Text::FontWeight{600});
    heading.Margin(Thickness{0, 0, 0, 4});
    group.Children().Append(heading);
    body.Children().Append(group);
    return group;
}
void Append(StackPanel const &body, UIElement const &element) {
    if (element.try_as<TextBox>() || element.try_as<ComboBox>() || element.try_as<PasswordBox>())
        body.Children().Append(FieldRow(element.as<FrameworkElement>()));
    else
        body.Children().Append(element);
}
void ValidateText(const std::wstring &value, std::size_t maximum, PageId page,
                  const wchar_t *id, const wchar_t *label, bool required) {
    if (value.find(L'\0') != std::wstring::npos)
        throw ValidationError(page, id, std::wstring(label) + L"不能包含空字符（NUL）。");
    if (required && value.empty())
        throw ValidationError(page, id, std::wstring(L"请填写") + label + L"。");
    if (value.size() > maximum)
        throw ValidationError(page, id, std::wstring(label) + L"不能超过 " +
                                          std::to_wstring(maximum) + L" 个字符。");
}
std::uint32_t Unsigned32(TextBox const &field, PageId page, const wchar_t *label) {
    return static_cast<std::uint32_t>(Number(field, (std::numeric_limits<std::uint32_t>::max)(),
                                              page, label));
}
std::uint64_t Unsigned64(TextBox const &field, PageId page, const wchar_t *label) {
    return Number(field, (std::numeric_limits<std::uint64_t>::max)(), page, label);
}
bool SameAccount(const std::wstring &left, const std::wstring &right) {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
std::wstring PickPath(HWND owner, PickerKind kind) {
    com_ptr<IFileDialog> picker;
    if (kind == PickerKind::Output) {
        com_ptr<IFileSaveDialog> save;
        check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(save.put())));
        picker = save.as<IFileDialog>();
    } else {
        com_ptr<IFileOpenDialog> open;
        check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(open.put())));
        picker = open.as<IFileDialog>();
    }
    FILEOPENDIALOGOPTIONS options{};
    check_hresult(picker->GetOptions(&options));
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT;
    if (kind == PickerKind::Directory)
        options |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
    else if (kind != PickerKind::Output)
        options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    check_hresult(picker->SetOptions(options));
    if (kind == PickerKind::Application) {
        const COMDLG_FILTERSPEC filters[]{{L"可执行程序", L"*.exe;*.com;*.cmd;*.bat"},
                                          {L"所有文件", L"*.*"}};
        check_hresult(picker->SetFileTypes(2, filters));
    }
    const auto result = picker->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check_hresult(result);
    com_ptr<IShellItem> item;
    check_hresult(picker->GetResult(item.put()));
    PWSTR raw = nullptr;
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &raw));
    std::wstring path;
    try { path.assign(raw); }
    catch (...) { CoTaskMemFree(raw); throw; }
    CoTaskMemFree(raw);
    return path;
}

void EditorPages::Initialize(const ServiceConfiguration &configuration, bool isNew, HWND hwnd) {
    original = configuration;
    is_new = isNew;
    owner = hwnd;
    for (std::size_t i = 0; i < PageCount; ++i) {
        auto &page = pages[i];
        page.body = StackPanel();
        page.body.Spacing(16);
        page.body.Padding(Thickness{16, 16, 16, 20});
        // The selected native tab is the page heading; do not repeat a large
        // title above every compact group of fields.
        page.scroll = ScrollViewer();
        page.scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        page.scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        page.scroll.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        page.scroll.Content(page.body);
        Id(page.scroll, std::wstring(L"EditorPage") + Keys[i]);
    }
    BuildApplication(); BuildDetails(); BuildLogon(); BuildDependencies();
    BuildProcess(); BuildShutdown(); BuildExit(); BuildIo(); BuildRotation();
    BuildEnvironment(); BuildHooks();
    baseline.reserve(snapshots.size());
    for (const auto &snapshot : snapshots)
        baseline.push_back(snapshot());
    initialized = true;
    UpdateEnabled();
}
void EditorPages::Register(FrameworkElement const &control) {
    const auto id = ControlId(control);
    if (!id.empty())
        focus_targets.insert_or_assign(id, control);
}
void EditorPages::Watch(TextBox const &control) {
    Register(control);
    snapshots.push_back([control] { return Text(control.Text()); });
    std::weak_ptr<EditorPages> weak = shared_from_this();
    control.TextChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->OnChanged();
    });
}
void EditorPages::Watch(CheckBox const &control) {
    Register(control);
    snapshots.push_back([control] { return Checked(control) ? L"1" : L"0"; });
    std::weak_ptr<EditorPages> weak = shared_from_this();
    const auto handler = [weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->OnChanged();
    };
    control.Checked(handler);
    control.Unchecked(handler);
}
void EditorPages::Watch(ComboBox const &control) {
    Register(control);
    snapshots.push_back([control] { return std::to_wstring(control.SelectedIndex()); });
    std::weak_ptr<EditorPages> weak = shared_from_this();
    control.SelectionChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->OnChanged();
    });
}
void EditorPages::Watch(PasswordBox const &control) {
    Register(control);
    // Only presence, never a password or hash, enters the dirty-state baseline.
    snapshots.push_back([control] { return control.Password().empty() ? L"0" : L"1"; });
    std::weak_ptr<EditorPages> weak = shared_from_this();
    control.PasswordChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->OnChanged();
    });
}
void EditorPages::OnChanged() {
    if (!initialized) return;
    UpdateEnabled();
    if (changed) changed();
}
bool EditorPages::IsDirty() const {
    if (credentials_extracted) return true;
    for (std::size_t i = 0; i < snapshots.size(); ++i)
        if (snapshots[i]() != baseline[i]) return true;
    return false;
}
void EditorPages::UpdateEnabled() {
    const bool custom = account_kind.SelectedIndex() == static_cast<int>(AccountKind::Custom);
    username.IsEnabled(custom);
    password.IsEnabled(custom);
    confirmation.IsEnabled(custom);
    grant_logon.IsEnabled(custom);
    password_notice.Text(L"密码已清除，如需再次提交请重新输入。");
    password_notice.Visibility(custom && credentials_extracted ? Visibility::Visible : Visibility::Collapsed);
    affinity.IsEnabled(!Checked(all_cpus));
    console_ms.IsEnabled(Checked(stop_console));
    window_ms.IsEnabled(Checked(stop_window));
    threads_ms.IsEnabled(Checked(stop_threads));
    const bool rotating = Checked(rotate);
    online.IsEnabled(rotating);
    older_than.IsEnabled(rotating);
    larger_than.IsEnabled(rotating);
    rotation_delay.IsEnabled(rotating);
    stdout_disposition.IsEnabled(!streams[1].Text().empty());
    stderr_disposition.IsEnabled(!streams[2].Text().empty());
    replacement.IsEnabled(Checked(replacement_enabled));
    extra.IsEnabled(Checked(extra_enabled));
    dependency_remove.IsEnabled(dependency_list.SelectedIndex() >= 0);
}
void EditorPages::FocusField(const std::wstring &id) {
    for (std::size_t i = 0; i < hook_commands.size(); ++i)
        if (ControlId(hook_commands[i]) == id) RevealHook(i);
    std::weak_ptr<EditorPages> weak = shared_from_this();
    const auto found = focus_targets.find(id);
    if (found == focus_targets.end()) return;
    found->second.DispatcherQueue().TryEnqueue([weak, id] {
        if (const auto state = weak.lock()) {
            const auto target = state->focus_targets.find(id);
            if (target != state->focus_targets.end()) {
                target->second.StartBringIntoView();
                auto control = target->second.try_as<Control>();
                if (control && !control.IsEnabled()) {
                    // Keep the invalid draft intact; focus its enabling toggle
                    // rather than silently changing a configuration boolean.
                    std::wstring toggle;
                    if (id == L"EditorAffinity") toggle = L"EditorAllProcessors";
                    else if (id == L"EditorStopConsoleTimeout") toggle = L"EditorStopConsole";
                    else if (id == L"EditorStopWindowTimeout") toggle = L"EditorStopWindow";
                    else if (id == L"EditorStopThreadsTimeout") toggle = L"EditorStopThreads";
                    else if (id == L"EditorRotateSeconds" || id == L"EditorRotateBytes" || id == L"EditorRotateDelay")
                        toggle = L"EditorRotateFiles";
                    else if (id == L"EditorEnvironmentReplacement") toggle = L"EditorEnvironmentReplacementEnabled";
                    else if (id == L"EditorEnvironmentExtra") toggle = L"EditorEnvironmentExtraEnabled";
                    const auto enabling = state->focus_targets.find(toggle);
                    if (enabling != state->focus_targets.end()) control = enabling->second.try_as<Control>();
                }
                if (control) control.Focus(FocusState::Programmatic);
            }
        }
    });
}
void EditorPages::Browse(Button const &button, TextBox const &target, PickerKind kind,
                         PageId page, bool quote) {
    const auto id = ControlId(target);
    std::weak_ptr<EditorPages> weak = shared_from_this();
    button.Click([weak, id, kind, page, quote](auto const &, auto const &) {
        if (const auto state = weak.lock()) {
            try {
                auto path = PickPath(state->owner, kind);
                if (!path.empty()) {
                    if (quote) path = L"\"" + path + L"\"";
                    const auto field = state->focus_targets.at(id).as<TextBox>();
                    if (path.size() > static_cast<std::size_t>(field.MaxLength()))
                        throw ServiceError(L"所选路径超过此字段的长度限制；原有输入未改变。");
                    field.Text(path);
                    if (kind == PickerKind::Application && id == L"EditorApplication")
                        state->UpdateApplicationDefaults();
                }
            } catch (ServiceError const &error) {
                if (state->show_error) state->show_error(error.message, PageKey(page));
            } catch (hresult_error const &error) {
                if (state->show_error) state->show_error(Text(error.message()), PageKey(page));
            } catch (...) {
                if (state->show_error)
                    state->show_error(L"无法打开文件选择器。输入草稿未改变。", PageKey(page));
            }
        }
    });
}
ServiceConfiguration EditorPages::Read() const {
    // Starting from the original preserves unexposed stream flags, skip bits and
    // disabled values. Individual pages overwrite only their actual UI fields.
    auto result = original;
    ReadBasic(result);
    ReadLoginDependencies(result);
    ReadProcess(result);
    ReadOutput(result);
    ReadEnvironmentHooks(result);
    ValidateCredentials();
    return result;
}
std::wstring EditorPages::DraftAccount() const {
    auto login = original.login;
    if (account_kind.SelectedIndex() >= 0 && account_kind.SelectedIndex() <= 4)
        login.kind = static_cast<AccountKind>(account_kind.SelectedIndex());
    if (login.kind == AccountKind::Custom) login.username = Text(username.Text());
    return AccountName(login, Text(name.Text()));
}
bool EditorPages::SystemAccount() const { return account_kind.SelectedIndex() == 0; }
} // namespace nssm_next::ui::editor_pages
