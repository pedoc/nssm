#include "pch.h"
#include "ServiceEditor.h"
#include "../Core/NativeServices.h"
#include "../Core/ServicePolicy.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <array>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace {
std::wstring Text(hstring const &value) {
    return {value.c_str(), value.size()};
}
void Field(TextBox const &box, const wchar_t *header, const wchar_t *id, int length) {
    box.Header(box_value(header));
    box.MaxLength(length);
    box.HorizontalAlignment(HorizontalAlignment::Stretch);
    Automation::AutomationProperties::SetAutomationId(box, id);
}
std::wstring PickExecutable(HWND owner) {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW options{sizeof(OPENFILENAMEW)};
    options.hwndOwner = owner;
    options.lpstrFile = path.data();
    options.nMaxFile = static_cast<DWORD>(path.size());
    options.lpstrFilter = L"可执行程序 (*.exe)\0*.exe\0所有文件\0*.*\0\0";
    options.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&options))
        return path.data();
    const auto error = CommDlgExtendedError();
    if (error)
        throw nssm_next::ServiceError(L"无法打开文件选择器（" + std::to_wstring(error) + L"）。");
    return {};
}
std::wstring PickDirectory(HWND owner) {
    com_ptr<IFileOpenDialog> picker;
    check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(picker.put())));
    FILEOPENDIALOGOPTIONS options{};
    check_hresult(picker->GetOptions(&options));
    check_hresult(picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                     FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR));
    const auto result = picker->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check_hresult(result);
    com_ptr<IShellItem> item;
    check_hresult(picker->GetResult(item.put()));
    PWSTR raw = nullptr;
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &raw));
    const std::wstring path(raw);
    CoTaskMemFree(raw);
    return path;
}
} // namespace

namespace nssm_next::ui {
ServiceConfiguration ServiceEditor::Configuration() const {
    ServiceConfiguration result;
    result.name = Text(name.Text());
    result.display_name = Text(displayName.Text());
    result.application = Text(application.Text());
    result.arguments = Text(arguments.Text());
    result.working_directory = Text(directory.Text());
    result.description = Text(description.Text());
    result.startup = static_cast<StartupMode>(startup.SelectedIndex());
    return NormalizeConfiguration(result);
}
void ServiceEditor::ShowError(const std::wstring &message) {
    error.Message(message);
    error.IsOpen(true);
}
std::shared_ptr<ServiceEditor> MakeServiceEditor(const ServiceConfiguration &configuration,
                                                 bool isNew, XamlRoot const &root, HWND owner,
                                                 const std::wstring &account) {
    auto editor = std::make_shared<ServiceEditor>();
    editor->dialog.XamlRoot(root);
    editor->dialog.Title(box_value(isNew ? L"新增 NssmNext 服务" : L"编辑服务配置"));
    editor->dialog.PrimaryButtonText(isNew ? L"创建服务" : L"保存更改");
    editor->dialog.CloseButtonText(L"取消");
    editor->dialog.DefaultButton(ContentDialogButton::Primary);
    Automation::AutomationProperties::SetAutomationId(editor->dialog, L"ServiceEditorDialog");
    StackPanel content;
    content.Spacing(12);
    content.MinWidth(480);
    Field(editor->name, L"服务名（创建后不可修改）", L"EditorServiceName", 256);
    Field(editor->displayName, L"显示名称", L"EditorDisplayName", 256);
    Field(editor->application, L"应用程序完整路径", L"EditorApplication", 32766);
    Field(editor->arguments, L"启动参数（按原样传递）", L"EditorArguments", 32766);
    Field(editor->directory, L"工作目录（留空使用程序所在目录）", L"EditorDirectory", 32755);
    Field(editor->description, L"服务描述", L"EditorDescription", 4096);
    editor->name.Text(configuration.name);
    editor->name.IsEnabled(isNew);
    editor->displayName.Text(configuration.display_name);
    editor->application.Text(configuration.application);
    editor->arguments.Text(configuration.arguments);
    editor->directory.Text(configuration.working_directory);
    editor->description.Text(configuration.description);
    editor->description.AcceptsReturn(true);
    editor->description.TextWrapping(TextWrapping::Wrap);
    editor->description.Height(72);
    editor->startup.Header(box_value(L"启动类型"));
    editor->startup.HorizontalAlignment(HorizontalAlignment::Stretch);
    Automation::AutomationProperties::SetAutomationId(editor->startup, L"EditorStartup");
    for (const auto *label : {L"自动", L"自动（延迟启动）", L"手动", L"禁用"})
        editor->startup.Items().Append(box_value(label));
    editor->startup.SelectedIndex(static_cast<int>(configuration.startup));
    editor->error.Title(L"无法保存");
    editor->error.Severity(InfoBarSeverity::Error);
    editor->error.IsOpen(false);
    editor->error.IsClosable(false);
    Automation::AutomationProperties::SetAutomationId(editor->error, L"EditorError");
    InfoBar accountNotice;
    accountNotice.IsOpen(true);
    accountNotice.IsClosable(false);
    accountNotice.Severity(isNew ? InfoBarSeverity::Warning : InfoBarSeverity::Informational);
    accountNotice.Title(isNew ? L"运行账户：LocalSystem" : L"运行账户：" + account);
    accountNotice.Message(
        isNew ? L"拥有较高系统权限。请仅选择可信程序并确认文件权限；创建后不会自动启动。"
              : L"保存不会自动重启服务；运行中的程序需重启后加载新配置。");
    content.Children().Append(accountNotice);
    content.Children().Append(editor->error);
    content.Children().Append(editor->name);
    content.Children().Append(editor->displayName);
    Grid applicationRow;
    ColumnDefinition fill;
    fill.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition buttonColumn;
    buttonColumn.Width(GridLengthHelper::Auto());
    applicationRow.ColumnDefinitions().Append(fill);
    applicationRow.ColumnDefinitions().Append(buttonColumn);
    applicationRow.ColumnSpacing(8);
    applicationRow.Children().Append(editor->application);
    Button browse;
    browse.Content(box_value(L"浏览…"));
    browse.VerticalAlignment(VerticalAlignment::Bottom);
    Grid::SetColumn(browse, 1);
    applicationRow.Children().Append(browse);
    std::weak_ptr<ServiceEditor> weak = editor;
    browse.Click([weak, owner](auto const &, auto const &) {
        if (const auto state = weak.lock()) {
            try {
                const auto path = PickExecutable(owner);
                if (!path.empty())
                    state->application.Text(path);
            } catch (ServiceError const &error) {
                state->ShowError(error.message);
            } catch (hresult_error const &error) {
                state->ShowError(Text(error.message()));
            }
        }
    });
    content.Children().Append(applicationRow);
    content.Children().Append(editor->arguments);
    Grid directoryRow;
    ColumnDefinition directoryFill;
    directoryFill.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition directoryButton;
    directoryButton.Width(GridLengthHelper::Auto());
    directoryRow.ColumnDefinitions().Append(directoryFill);
    directoryRow.ColumnDefinitions().Append(directoryButton);
    directoryRow.ColumnSpacing(8);
    directoryRow.Children().Append(editor->directory);
    Button folder;
    folder.Content(box_value(L"浏览…"));
    folder.VerticalAlignment(VerticalAlignment::Bottom);
    Grid::SetColumn(folder, 1);
    directoryRow.Children().Append(folder);
    folder.Click([weak, owner](auto const &, auto const &) {
        if (const auto state = weak.lock()) {
            try {
                const auto path = PickDirectory(owner);
                if (!path.empty())
                    state->directory.Text(path);
            } catch (ServiceError const &error) {
                state->ShowError(error.message);
            } catch (hresult_error const &error) {
                state->ShowError(Text(error.message()));
            }
        }
    });
    content.Children().Append(directoryRow);
    content.Children().Append(editor->startup);
    content.Children().Append(editor->description);
    ScrollViewer scroll;
    scroll.MaxHeight(600);
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroll.Content(content);
    editor->dialog.Content(scroll);
    return editor;
}
bool RelaunchElevated(HWND owner) {
    const auto executable = CurrentExecutablePath();
    const auto directory = executable.substr(0, executable.find_last_of(L"\\/"));
    SHELLEXECUTEINFOW launch{sizeof(SHELLEXECUTEINFOW)};
    launch.hwnd = owner;
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = L"runas";
    launch.lpFile = executable.c_str();
    launch.lpParameters = L"--ui";
    launch.lpDirectory = directory.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED)
            return false;
        throw ServiceError(L"无法以管理员身份重新启动程序（" + std::to_wstring(error) + L"）。",
                           error);
    }
    if (launch.hProcess)
        CloseHandle(launch.hProcess);
    return true;
}
std::wstring TestApplicationPath() {
    std::array<wchar_t, 32768> directory{};
    const auto size = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (!size || size >= directory.size())
        throw ServiceError(L"无法定位系统测试程序。");
    return std::wstring(directory.data(), size) + L"\\cmd.exe";
}
} // namespace nssm_next::ui
