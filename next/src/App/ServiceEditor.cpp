#include "pch.h"
#include "ServiceEditor.h"
#include "ServiceEditorPages/EditorPages.h"
#include "../Core/NativeServices.h"
#include "../Core/ServicePolicy.h"
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <algorithm>
#include <array>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace pages = nssm_next::ui::editor_pages;

namespace {
void AddRow(Grid const &grid, GridLength length) {
    RowDefinition row;
    row.Height(length);
    grid.RowDefinitions().Append(row);
}
void AddColumn(Grid const &grid, GridLength length) {
    ColumnDefinition column;
    column.Width(length);
    grid.ColumnDefinitions().Append(column);
}
Grid ThemeRoot() {
    // A real ThemeResource expression follows this window's requested/actual
    // theme; resolving a brush once from Application.Resources would not.
    return Markup::XamlReader::Load(
        LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}" />)").as<Grid>();
}
void Accent(Button const &button) {
    if (const auto resource = Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")))
        if (const auto style = resource.try_as<Style>()) button.Style(style);
}
struct ErrorLocation { pages::PageId page; const wchar_t *field; };
ErrorLocation LocateError(const std::wstring &message) {
    const auto has = [&message](const wchar_t *text) { return message.find(text) != std::wstring::npos; };
    if (has(L"依赖") || has(L"dependen")) return {pages::PageId::Dependencies, L"EditorDependencyName"};
    if (has(L"账户") || has(L"帐户") || has(L"密码") || has(L"登录")) return {pages::PageId::Logon, L"EditorAccountKind"};
    if (has(L"环境") || has(L"Environment")) return {pages::PageId::Environment, L"EditorEnvironmentReplacementEnabled"};
    if (has(L"钩子") || has(L"Hook")) return {pages::PageId::Hooks, L"EditorHookEvent"};
    if (has(L"轮转") || has(L"Rotate")) return {pages::PageId::Rotation, L"EditorRotateFiles"};
    if (has(L"优先级") || has(L"亲和") || has(L"CPU")) return {pages::PageId::Process, L"EditorPriority"};
    if (has(L"宽限") || has(L"关闭方式")) return {pages::PageId::Shutdown, L"EditorStopConsole"};
    if (has(L"节流") || has(L"重启延迟") || has(L"退出动作")) return {pages::PageId::Exit, L"EditorExitAction"};
    if (has(L"标准输入") || has(L"标准输出") || has(L"标准错误")) return {pages::PageId::Io, L"EditorStdin"};
    if (has(L"显示名称")) return {pages::PageId::Details, L"EditorDisplayName"};
    if (has(L"描述")) return {pages::PageId::Details, L"EditorDescription"};
    if (has(L"启动类型")) return {pages::PageId::Details, L"EditorStartup"};
    if (has(L"工作目录")) return {pages::PageId::Application, L"EditorDirectory"};
    if (has(L"参数") || has(L"命令行")) return {pages::PageId::Application, L"EditorArguments"};
    return {pages::PageId::Application, L"EditorApplication"};
}
void PositionWindow(HWND window, HWND owner) {
    const auto dpi = GetDpiForWindow(owner);
    int width = MulDiv(960, dpi ? static_cast<int>(dpi) : 96, 96);
    int height = MulDiv(640, dpi ? static_cast<int>(dpi) : 96, 96);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    const auto handle = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    RECT parent{};
    GetWindowRect(owner, &parent);
    int left = parent.left + (parent.right - parent.left - width) / 2;
    int top = parent.top + (parent.bottom - parent.top - height) / 2;
    if (GetMonitorInfoW(handle, &monitor)) {
        const auto work = monitor.rcWork;
        width = (std::min)(width, static_cast<int>(work.right - work.left));
        height = (std::min)(height, static_cast<int>(work.bottom - work.top));
        left = (std::clamp)(left, static_cast<int>(work.left), static_cast<int>(work.right) - width);
        top = (std::clamp)(top, static_cast<int>(work.top), static_cast<int>(work.bottom) - height);
    }
    SetWindowPos(window, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    for (const auto kind : {ICON_SMALL, ICON_BIG}) {
        const auto icon = SendMessageW(owner, WM_GETICON, kind, 0);
        if (icon) SendMessageW(window, WM_SETICON, kind, icon);
    }
}
} // namespace

namespace nssm_next::ui {
ServiceEditor::~ServiceEditor() {
    // ShowAsync holds this object through Closed, so normal destruction happens
    // back on the UI apartment. Also clean up an editor abandoned before ShowAsync.
    try {
        if (m_pages) m_pages->ClearSecrets();
        if (m_window && !m_closed) {
            m_allowClose = true;
            m_window.Close();
        }
    } catch (...) {}
}
void ServiceEditor::Initialize(const ServiceConfiguration &configuration, bool isNew,
                                XamlRoot const &parentRoot, HWND owner,
                                const std::wstring &currentAccount) {
    DWORD process = 0;
    if (!owner || !IsWindow(owner) || !GetWindowThreadProcessId(owner, &process) ||
        process != GetCurrentProcessId())
        throw ServiceError(L"编辑器必须由当前应用的主窗口拥有，不能使用其他进程的窗口。");
    m_isNew = isNew;
    m_closedEvent.attach(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    check_bool(static_cast<bool>(m_closedEvent));
    m_window = Window();
    m_window.Title(isNew ? L"新增 NssmNext 服务" : L"编辑服务 — " + configuration.name);
#ifdef NSSM_NEXT_UI_TEST_BACKEND
    m_window.Title(m_window.Title() + L" [内存测试]");
#endif
    check_hresult(m_window.as<IWindowNative>()->get_WindowHandle(&m_hwnd));
    SetLastError(ERROR_SUCCESS);
    const auto previous = SetWindowLongPtrW(m_hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    if (!previous && GetLastError() != ERROR_SUCCESS) throw_last_error();

    m_root = ThemeRoot();
    pages::Id(m_root, L"ServiceEditorWindow");
    if (parentRoot)
        if (const auto parent = parentRoot.Content().try_as<FrameworkElement>())
            m_root.RequestedTheme(parent.RequestedTheme());
    AddRow(m_root, GridLengthHelper::Auto());
    AddRow(m_root, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    AddRow(m_root, GridLengthHelper::Auto());

    // The window caption already identifies create/edit. Keep the security
    // notice compact so the form, rather than a second heading, owns the space.
    StackPanel header;
    header.Padding(Thickness{16, 8, 16, 8});
    header.Spacing(6);
    m_accountNotice = Markup::XamlReader::Load(
        LR"(<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" FontSize="12" Foreground="{ThemeResource TextFillColorSecondaryBrush}" TextWrapping="Wrap" />)").as<TextBlock>();
    m_accountNotice.Text(L"运行账户：" + currentAccount);
    pages::Id(m_accountNotice, L"EditorAccountNotice");
    pages::Help(m_accountNotice, L"运行账户需要具有程序、目录、日志和钩子的访问权限。保存不会自动重启服务。");
    header.Children().Append(m_accountNotice);
    m_error = InfoBar();
    m_error.Title(L"无法保存配置");
    m_error.Severity(InfoBarSeverity::Error);
    m_error.IsOpen(false);
    m_error.IsClosable(true);
    pages::Id(m_error, L"EditorError");
    header.Children().Append(m_error);
    m_root.Children().Append(header);

    m_workspace = Grid();
    m_workspace.Margin(Thickness{16, 0, 16, 0});
    AddRow(m_workspace, GridLengthHelper::Auto());
    AddRow(m_workspace, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    // SelectorBar is page navigation, not a strip of document tabs. Its native
    // selected indicator and hover states follow the active WinUI theme.
    m_navigation = SelectorBar();
    m_navigation.HorizontalAlignment(HorizontalAlignment::Left);
    pages::Id(m_navigation, L"EditorNavigation");
    Automation::AutomationProperties::SetName(m_navigation, L"服务配置类别");
    m_pages = std::make_shared<pages::EditorPages>();
    m_pages->Initialize(configuration, isNew, m_hwnd);
    Grid pageHost;
    pages::Id(pageHost, L"EditorPageHost");
    Grid::SetRow(pageHost, 1);
    for (std::size_t i = 0; i < pages::PageCount; ++i) {
        const auto page = static_cast<pages::PageId>(i);
        SelectorBarItem item;
        item.Text(pages::PageTitle(page));
        item.FontSize(14);
        item.Padding(Thickness{12, 10, 12, 10});
        item.MinHeight(44);
        pages::Id(item, std::wstring(L"EditorNav") + pages::PageKey(page));
        m_navigation.Items().Append(item);
        // Retain inactive controls and their drafts without rebuilding pages.
        m_pages->pages[i].scroll.Visibility(Visibility::Collapsed);
        pageHost.Children().Append(m_pages->pages[i].scroll);
    }
    ScrollViewer navigationScroll;
    navigationScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    navigationScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
    navigationScroll.VerticalScrollMode(ScrollMode::Disabled);
    navigationScroll.Content(m_navigation);
    const auto navigationBorder = Markup::XamlReader::Load(
        LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" BorderBrush="{ThemeResource DividerStrokeColorDefaultBrush}" BorderThickness="0,0,0,1" Padding="0,0,0,6" />)").as<Border>();
    navigationBorder.Child(navigationScroll);
    m_workspace.Children().Append(navigationBorder);
    m_workspace.Children().Append(pageHost);
    m_workspaceHost = ContentControl();
    m_workspaceHost.Content(m_workspace);
    m_workspaceHost.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    m_workspaceHost.VerticalContentAlignment(VerticalAlignment::Stretch);
    Grid::SetRow(m_workspaceHost, 1);
    m_root.Children().Append(m_workspaceHost);

    Border footer;
    footer.Padding(Thickness{16, 12, 16, 10});
    footer.BorderThickness(Thickness{0, 1, 0, 0});
    if (const auto resource = Application::Current().Resources().TryLookup(box_value(L"DividerStrokeColorDefaultBrush")))
        if (const auto brush = resource.try_as<Media::Brush>()) footer.BorderBrush(brush);
    Grid::SetRow(footer, 2);
    Grid buttons;
    buttons.ColumnSpacing(8);
    AddColumn(buttons, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    AddColumn(buttons, GridLengthHelper::Auto());
    AddColumn(buttons, GridLengthHelper::Auto());
    AddRow(buttons, GridLengthHelper::Auto());
    AddRow(buttons, GridLengthHelper::Auto());
    // This is the very same watched/validated name control created by the
    // Application page, not a second draft or a copied footer value.
    const auto serviceName = m_pages->ServiceNameField();
    ToolTipService::SetToolTip(serviceName, box_value(L"内部服务名称；创建后不可修改。"));
    const auto nameRow = pages::FieldRow(serviceName, nullptr, 84);
    nameRow.Margin(Thickness{0, 0, 16, 0});
    buttons.Children().Append(nameRow);
    StackPanel status;
    status.Orientation(Orientation::Horizontal);
    status.Spacing(8);
    status.VerticalAlignment(VerticalAlignment::Center);
    status.Margin(Thickness{96, 4, 0, 0});
    Grid::SetRow(status, 1);
    Grid::SetColumnSpan(status, 3);
    m_progress = ProgressRing();
    m_progress.Width(20); m_progress.Height(20);
    m_progress.Visibility(Visibility::Collapsed);
    m_progress.IsActive(false);
    pages::Id(m_progress, L"EditorSaveProgress");
    m_status = pages::Note(L"");
    m_status.VerticalAlignment(VerticalAlignment::Center);
    pages::Id(m_status, L"EditorDirtyState");
    status.Children().Append(m_progress);
    status.Children().Append(m_status);
    buttons.Children().Append(status);
    m_save = Button();
    m_save.Content(box_value(isNew ? L"创建服务" : L"保存更改"));
    m_save.MinWidth(104);
    m_save.VerticalAlignment(VerticalAlignment::Top);
    pages::Id(m_save, L"EditorSave");
    Accent(m_save);
    Grid::SetColumn(m_save, 1);
    buttons.Children().Append(m_save);
    m_cancel = Button();
    m_cancel.Content(box_value(L"取消"));
    m_cancel.MinWidth(80);
    m_cancel.VerticalAlignment(VerticalAlignment::Top);
    pages::Id(m_cancel, L"EditorCancel");
    Grid::SetColumn(m_cancel, 2);
    buttons.Children().Append(m_cancel);
    footer.Child(buttons);
    m_root.Children().Append(footer);
    m_window.Content(m_root);

    const std::weak_ptr<ServiceEditor> weak = shared_from_this();
    m_pages->changed = [weak] {
        if (const auto state = weak.lock()) state->UpdateStatus();
    };
    m_pages->show_error = [weak](const std::wstring &message, const std::wstring &page) {
        if (const auto state = weak.lock()) state->ShowError(message, page);
    };
    m_navigation.SelectionChanged([weak](auto const &, auto const &) {
        if (const auto state = weak.lock()) {
            const auto selected = state->m_navigation.SelectedItem();
            for (std::uint32_t i = 0; i < pages::PageCount; ++i)
                if (state->m_navigation.Items().GetAt(i) == selected) {
                    state->SelectPage(i);
                    break;
                }
        }
    });
    m_save.Click([weak](auto const &, auto const &) {
        if (const auto state = weak.lock()) state->Save();
    });
    m_cancel.Click([weak](auto const &, auto const &) {
        if (const auto state = weak.lock()) state->RequestClose();
    });
    m_window.AppWindow().Closing([weak](auto const &, auto const &args) {
        if (const auto state = weak.lock()) {
            if (state->m_saving || state->m_confirming) {
                args.Cancel(true);
                if (state->m_saving) state->m_status.Text(L"正在保存，请等待操作完成后再关闭。");
            } else if (!state->m_allowClose && state->IsDirty()) {
                args.Cancel(true);
                state->RequestClose();
            }
        }
    });
    m_window.Closed([weak](auto const &, auto const &) {
        if (const auto state = weak.lock()) {
            state->m_closed = true;
            state->m_saving = false;
            state->m_pages->ClearSecrets();
            // Do not destroy a caller's callback while it may be returning from
            // CloseSaved. Its lifetime follows the editor, not this event.
            SetEvent(state->m_closedEvent.get());
        }
    });
    SelectPage(0);
    UpdateStatus();
    PositionWindow(m_hwnd, owner);
}
void ServiceEditor::SelectPage(std::size_t index) {
    if (index >= pages::PageCount || m_closed) return;
    for (std::size_t i = 0; i < pages::PageCount; ++i)
        m_pages->pages[i].scroll.Visibility(i == index ? Visibility::Visible : Visibility::Collapsed);
    const auto item = m_navigation.Items().GetAt(static_cast<std::uint32_t>(index));
    if (m_navigation.SelectedItem() != item)
        m_navigation.SelectedItem(item);
    item.StartBringIntoView();
}
void ServiceEditor::UpdateStatus() {
    if (m_closed) return;
    std::wstring account;
    try { account = m_pages->DraftAccount(); } catch (...) {}
    m_accountNotice.Text(L"运行账户：" + (account.empty() ? std::wstring(L"（待填写）") : account) +
        (m_pages->SystemAccount() ? L" · 高权限，请仅运行可信程序" : L""));
    const bool dirty = IsDirty();
    m_status.Text(m_saving ? L"正在保存…" : dirty ? L"有未保存的更改" : L"");
    m_status.Visibility(m_saving || dirty ? Visibility::Visible : Visibility::Collapsed);
}
ServiceConfiguration ServiceEditor::Configuration() const {
    try { return NormalizeConfiguration(m_pages->Read()); }
    catch (pages::ValidationError const &) { throw; }
    catch (ServiceError const &error) {
        const auto location = LocateError(error.message);
        throw pages::ValidationError(location.page, location.field, error.message);
    }
}
CredentialRequest ServiceEditor::TakeCredentials() { return m_pages->TakeCredentials(); }
bool ServiceEditor::IsDirty() const { return m_pages && m_pages->IsDirty(); }
void ServiceEditor::ShowError(const std::wstring &message, const std::wstring &page) {
    if (m_closed) return;
    m_error.Message(message);
    m_error.IsOpen(true);
    if (!page.empty()) {
        const auto index = pages::FindPage(page);
        if (index < pages::PageCount) {
            SelectPage(index);
            const auto first = m_pages->pages[index].first;
            if (first) m_pages->FocusField(pages::Text(Automation::AutomationProperties::GetAutomationId(first)));
        }
    } else {
        const auto location = LocateError(message);
        SelectPage(static_cast<std::size_t>(location.page));
        m_pages->FocusField(location.field);
    }
}
void ServiceEditor::SetSaving(bool saving) {
    if (m_closed) return;
    m_saving = saving;
    m_workspaceHost.IsEnabled(!saving);
    m_pages->ServiceNameField().IsEnabled(!saving);
    m_save.IsEnabled(!saving);
    m_cancel.IsEnabled(!saving);
    m_progress.IsActive(saving);
    m_progress.Visibility(saving ? Visibility::Visible : Visibility::Collapsed);
    if (saving) m_error.IsOpen(false);
    UpdateStatus();
}
void ServiceEditor::Save() {
    if (m_saving || m_confirming || m_closed) return;
    try {
        // Validate all pages and credentials before invoking the owner's async
        // writer. Configuration never contains a password; TakeCredentials is
        // a distinct, one-use extraction performed by that writer.
        (void)Configuration();
        if (!save_requested) {
            ShowError(L"尚未连接保存处理程序；没有执行任何服务操作。");
            return;
        }
        m_error.IsOpen(false);
        // Invoke the stable stored closure, not a temporary copy whose captures
        // could disappear if a caller launches asynchronous work from it.
        save_requested();
    } catch (pages::ValidationError const &error) {
        ShowError(error.message, pages::PageKey(error.page));
        m_pages->FocusField(error.field);
    } catch (ServiceError const &error) {
        ShowError(error.message);
    } catch (hresult_error const &error) {
        ShowError(pages::Text(error.message()));
    } catch (...) {
        ShowError(L"无法读取或提交输入。所有非密码草稿仍保留在此窗口。");
    }
}
void ServiceEditor::Activate() { if (!m_closed) m_window.Activate(); }
Windows::Foundation::IAsyncAction ServiceEditor::ShowAsync() {
    const auto lifetime = shared_from_this();
    apartment_context ui;
    if (m_closed) co_return;
    const bool first_show = !m_shown;
    m_shown = true;
    m_window.Activate();
    if (first_show) m_pages->FocusField(m_isNew ? L"EditorServiceName" : L"EditorApplication");
    // The manual-reset event also covers a close occurring before the wait is
    // registered. The shared lifetime keeps its handle valid until resumption.
    co_await resume_on_signal(m_closedEvent.get());
    co_await ui;
}
void ServiceEditor::CloseSaved() {
    if (m_closed) return;
    saved = true;
    SetSaving(false);
    m_allowClose = true;
    m_pages->ClearSecrets();
    m_window.Close();
}
void ServiceEditor::RequestClose() {
    if (m_closed || m_confirming) return;
    if (m_saving) {
        m_status.Text(L"正在保存，请等待操作完成后再关闭。");
        Activate();
        return;
    }
    if (!IsDirty()) {
        m_allowClose = true;
        m_pages->ClearSecrets();
        m_window.Close();
        return;
    }
    m_confirming = true;
    ConfirmDiscard(shared_from_this());
}
fire_and_forget ServiceEditor::ConfirmDiscard(std::shared_ptr<ServiceEditor> state) {
    try {
        ContentDialog confirmation;
        confirmation.XamlRoot(state->m_root.XamlRoot());
        confirmation.Title(box_value(L"放弃未保存的更改？"));
        confirmation.Content(pages::Note(L"所有页面的输入草稿将被丢弃，密码将从输入框清除。尚未保存的更改不会写入服务。"));
        confirmation.PrimaryButtonText(L"放弃更改");
        confirmation.CloseButtonText(L"继续编辑");
        confirmation.DefaultButton(ContentDialogButton::Close);
        pages::Id(confirmation, L"EditorDiscardConfirmation");
        const auto answer = co_await confirmation.ShowAsync();
        state->m_confirming = false;
        if (answer == ContentDialogResult::Primary && !state->m_saving && !state->m_closed) {
            state->m_allowClose = true;
            state->m_pages->ClearSecrets();
            state->m_window.Close();
        }
    } catch (...) {
        state->m_confirming = false;
        state->ShowError(L"无法显示放弃更改确认。输入草稿仍保留，请继续编辑或重试关闭。");
    }
}
std::shared_ptr<ServiceEditor> MakeServiceEditor(const ServiceConfiguration &configuration,
                                                bool isNew, XamlRoot const &parentRoot,
                                                HWND owner, const std::wstring &currentAccount) {
    const auto editor = std::shared_ptr<ServiceEditor>(new ServiceEditor());
    editor->Initialize(configuration, isNew, parentRoot, owner, currentAccount);
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
        if (error == ERROR_CANCELLED) return false;
        throw ServiceError(L"无法以管理员身份重新启动程序（" + std::to_wstring(error) + L"）。", error);
    }
    if (launch.hProcess) CloseHandle(launch.hProcess);
    return true;
}
std::wstring TestApplicationPath() {
    std::array<wchar_t, 32768> directory{};
    const auto size = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (!size || size >= directory.size()) throw ServiceError(L"无法定位系统测试程序。");
    return std::wstring(directory.data(), size) + L"\\cmd.exe";
}
} // namespace nssm_next::ui
