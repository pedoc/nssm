#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include "ServiceEditor.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#ifdef NSSM_NEXT_UI_TEST_BACKEND
#include "../../tests/Fakes/MemoryServices.h"
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using nssm_next::ServiceState;

namespace {
std::wstring StateName(ServiceState state) {
    switch (state) {
    case ServiceState::Running:
        return L"运行中";
    case ServiceState::Stopped:
        return L"已停止";
    case ServiceState::Paused:
        return L"已暂停";
    case ServiceState::StartPending:
        return L"启动中";
    case ServiceState::StopPending:
        return L"停止中";
    default:
        return L"状态转换中";
    }
}
std::wstring StartupName(nssm_next::StartupMode mode) {
    switch (mode) {
    case nssm_next::StartupMode::Automatic:
        return L"自动";
    case nssm_next::StartupMode::DelayedAutomatic:
        return L"自动（延迟启动）";
    case nssm_next::StartupMode::Manual:
        return L"手动";
    case nssm_next::StartupMode::Disabled:
        return L"禁用";
    default:
        return L"未知";
    }
}
std::wstring Key(nssm_next::ServiceInfo const &service) {
    return service.name + L"\n" + service.instance_id;
}
Grid ServiceColumns() {
    // Keep these aligned with the table header in MainWindow.xaml.
    Grid grid;
    grid.ColumnSpacing(12);
    for (const double width : {220.0, 88.0, 132.0, -1.0, 280.0}) {
        ColumnDefinition column;
        column.Width(GridLengthHelper::FromValueAndType(
            width < 0 ? 1 : width, width < 0 ? GridUnitType::Star : GridUnitType::Pixel));
        grid.ColumnDefinitions().Append(column);
    }
    return grid;
}
TextBlock Cell(const std::wstring &text, int column) {
    TextBlock cell;
    cell.Text(text);
    cell.VerticalAlignment(VerticalAlignment::Center);
    cell.TextTrimming(TextTrimming::CharacterEllipsis);
    ToolTipService::SetToolTip(cell, box_value(hstring(text)));
    Grid::SetColumn(cell, column);
    return cell;
}
void SetOriginalIcon(HWND window, int kind, int metric) {
    const auto icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                 GetSystemMetrics(metric), GetSystemMetrics(metric), LR_SHARED);
    if (icon)
        SendMessageW(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
}
} // namespace
namespace winrt::NssmNext::implementation {
MainWindow::MainWindow() {
#ifdef NSSM_NEXT_UI_TEST_BACKEND
    m_manager =
        nssm_next::testing::CreateMemoryServiceManager(nssm_next::ui::TestApplicationPath());
#else
    m_manager = nssm_next::CreateNativeServiceManager();
#endif
    InitializeComponent();
    check_hresult(m_inner.as<IWindowNative>()->get_WindowHandle(&m_owner));
    SetOriginalIcon(m_owner, ICON_SMALL, SM_CXSMICON);
    SetOriginalIcon(m_owner, ICON_BIG, SM_CXICON);
    const auto window = m_inner.as<Microsoft::UI::Xaml::Window>();
    const auto weak = get_weak();
    window.Closed([weak](auto const &, auto const &) {
        if (const auto self = weak.get())
            self->m_closed = true;
    });
    window.AppWindow().Closing([weak](auto const &, auto const &arguments) {
        if (const auto self = weak.get()) {
            if (self->m_mutating) {
                arguments.Cancel(true);
                self->ShowStatus(L"正在完成服务操作，请稍候再关闭窗口。");
            } else if (self->m_editor && self->m_editor->IsOpen()) {
                arguments.Cancel(true);
                self->m_editor->RequestClose();
                self->ShowStatus(L"请先关闭配置窗口，未保存草稿会请求确认。");
            }
        }
    });
    if (m_manager->IsTestBackend()) {
        window.Title(L"NSSM Next — 服务管理 [内存测试]");
        ModeBar().Title(L"内存测试模式");
        ModeBar().Message(L"所有操作仅修改当前进程内的测试数据，不会创建、修改或控制系统服务。");
        ElevateButton().Visibility(Visibility::Collapsed);
    } else if (m_manager->CanWrite()) {
        ModeBar().Visibility(Visibility::Collapsed);
        ElevateButton().Visibility(Visibility::Collapsed);
    } else {
        ModeBar().Title(L"查看模式");
        ModeBar().Severity(InfoBarSeverity::Informational);
        ModeBar().Message(L"新增、编辑、删除和启停操作需要管理员权限。");
    }
}
void MainWindow::Root_Loaded(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    if (m_loaded)
        return;
    m_loaded = true;
    Refresh();
}
void MainWindow::Refresh_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Refresh();
}
void MainWindow::Search_Changed(Windows::Foundation::IInspectable const &,
                                TextChangedEventArgs const &) {
    RenderServices();
}
void MainWindow::Selection_Changed(Windows::Foundation::IInspectable const &,
                                   SelectionChangedEventArgs const &) {
    if (!m_loaded)
        return;
    const auto selected = ServiceList().SelectedItem().try_as<ListViewItem>();
    if (selected)
        m_selected = std::wstring(unbox_value<hstring>(selected.Tag()));
    else
        m_selected.clear();
}
std::optional<nssm_next::ServiceInfo> MainWindow::Selected() {
    for (const auto &service : m_snapshot.services)
        if (Key(service) == m_selected)
            return service;
    return std::nullopt;
}
bool MainWindow::SelectService(const std::wstring &key) {
    if (!m_loaded || m_closed || m_busy || m_dialog_open)
        return false;
    // A row action targets its own immutable name/instance pair, not whichever
    // row happened to be selected before the button was clicked.
    for (const auto &entry : ServiceList().Items()) {
        const auto item = entry.try_as<ListViewItem>();
        if (item && std::wstring(unbox_value<hstring>(item.Tag())) == key) {
            ServiceList().SelectedItem(item);
            m_selected = key;
            return true;
        }
    }
    return false;
}
void MainWindow::SetBusy(bool busy, bool mutating) {
    m_busy = busy;
    m_mutating = busy && mutating;
    UpdateControls();
}
void MainWindow::UpdateControls() {
    if (!m_loaded || m_closed)
        return;
    const bool available = !m_busy && !m_dialog_open;
    RefreshButton().IsEnabled(available);
    NewButton().IsEnabled(available);
    ElevateButton().IsEnabled(available);
    SearchBox().IsEnabled(available);
    ServiceList().IsEnabled(available);
    for (const auto &row : m_rows) {
        row.edit.IsEnabled(available);
        row.remove.IsEnabled(available);
        row.start.IsEnabled(available && row.state == ServiceState::Stopped &&
                            row.startup != nssm_next::StartupMode::Disabled);
        const bool running = row.state == ServiceState::Running || row.state == ServiceState::Paused;
        row.stop.IsEnabled(available && running);
        row.restart.IsEnabled(available && running);
    }
}
void MainWindow::ShowStatus(const std::wstring &message, InfoBarSeverity severity) {
    if (m_closed)
        return;
    StatusMessage().Text(message);
    ToolTipService::SetToolTip(StatusMessage(), box_value(hstring(message)));
    StatusBar().Severity(severity);
    StatusBar().Title(severity == InfoBarSeverity::Error ? L"操作未完成" : L"提示");
    StatusBar().Message(message);
    StatusBar().IsOpen(severity != InfoBarSeverity::Informational);
}
void MainWindow::ShowError(const std::wstring &message) {
    ShowStatus(message, InfoBarSeverity::Error);
}
fire_and_forget MainWindow::Refresh(std::wstring message) {
    if (!m_loaded || m_busy || m_dialog_open || m_closed)
        co_return;
    const auto weak = get_weak();
    const auto dispatcher = DispatcherQueue();
    const auto manager = m_manager;
    SetBusy(true);
    ShowStatus(L"正在读取 NssmNext 服务列表…");
    co_await resume_background();
    nssm_next::ServiceSnapshot snapshot;
    std::wstring error;
    try {
        snapshot = manager->Read();
    } catch (nssm_next::ServiceError const &exception) {
        error = exception.message;
    } catch (std::exception const &) {
        error = L"读取服务列表时发生异常，请重试。";
    }
    dispatcher.TryEnqueue(
        [weak, snapshot = std::move(snapshot), error, message = std::move(message)]() mutable {
            if (const auto self = weak.get()) {
                if (self->m_closed)
                    return;
                self->SetBusy(false);
                if (!error.empty()) {
                    self->ShowError(message.empty() ? error : message + L" 操作已提交，但刷新失败，请手动刷新；不要重复提交密码或创建请求。 " + error);
                    return;
                }
                self->m_snapshot = std::move(snapshot);
                self->RenderServices();
                const auto skipped = self->m_snapshot.inaccessible_parameters;
                std::wstring status = L"仅显示由 NssmNext 创建的服务。";
                if (skipped)
                    status += L" 无法验证或读取的参数项：" + std::to_wstring(skipped) + L"。";
                if (!message.empty())
                    status += L" " + message;
                self->ShowStatus(status, skipped ? InfoBarSeverity::Warning : InfoBarSeverity::Informational);
            }
        });
}
void MainWindow::RenderServices() {
    if (!m_loaded || m_closed)
        return;
    const auto wanted = m_selected;
    ServiceList().Items().Clear();
    m_rows.clear();
    const auto filter = SearchBox().Text();
    const auto weak = get_weak();
    std::size_t visible = 0;
    for (const auto &service : m_snapshot.services) {
        const auto searchable = service.name + L" " + service.display_name + L" " +
                                service.application + L" " + service.description;
        if (!filter.empty() &&
            FindStringOrdinal(FIND_FROMSTART, searchable.c_str(), -1, filter.c_str(), -1, TRUE) < 0)
            continue;
        const auto key = Key(service);
        const auto content = ServiceColumns();
        StackPanel names;
        names.Spacing(2);
        names.VerticalAlignment(VerticalAlignment::Center);
        auto name = Cell(service.display_name.empty() ? service.name : service.display_name, 0);
        names.Children().Append(name);
        if (!service.display_name.empty() && service.display_name != service.name) {
            auto identifier = Cell(service.name, 0);
            identifier.FontSize(12);
            identifier.Opacity(0.65);
            names.Children().Append(identifier);
        }
        content.Children().Append(names);
        auto state = Cell(StateName(service.state), 1);
        ToolTipService::SetToolTip(state, box_value(hstring(L"运行账户：" + service.account +
            L"\n宿主进程 PID：" + std::to_wstring(service.process_id))));
        content.Children().Append(state);
        content.Children().Append(Cell(StartupName(service.startup), 2));
        content.Children().Append(Cell(service.application, 3));
        StackPanel actions;
        actions.Orientation(Orientation::Horizontal);
        actions.Spacing(4);
        actions.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(actions, 4);
        const auto addAction = [&](const wchar_t *label, const wchar_t *id, auto handler) {
            Button button;
            button.Content(box_value(label));
            button.Padding(Thickness{8, 5, 8, 5});
            button.MinWidth(50);
            Automation::AutomationProperties::SetAutomationId(button, id);
            Automation::AutomationProperties::SetName(button, std::wstring(label) + L" " + service.name);
            button.Click([weak, key, handler](auto const &sender, auto const &args) {
                if (const auto self = weak.get())
                    if (self->SelectService(key))
                        (self.get()->*handler)(sender, args);
            });
            actions.Children().Append(button);
            return button;
        };
        ServiceRow row;
        row.state = service.state;
        row.startup = service.startup;
        row.start = addAction(L"启动", L"StartButton", &MainWindow::Start_Click);
        row.stop = addAction(L"停止", L"StopButton", &MainWindow::Stop_Click);
        row.restart = addAction(L"重启", L"RestartButton", &MainWindow::Restart_Click);
        row.edit = addAction(L"编辑", L"EditButton", &MainWindow::Edit_Click);
        row.remove = addAction(L"删除", L"DeleteButton", &MainWindow::Delete_Click);
        m_rows.push_back(std::move(row));
        content.Children().Append(actions);
        ListViewItem item;
        item.Content(content);
        item.Tag(box_value(hstring(key)));
        Automation::AutomationProperties::SetName(
            item, service.display_name + L" · " + service.name + L" · " + StateName(service.state));
        ServiceList().Items().Append(item);
        ++visible;
        if (key == wanted)
            ServiceList().SelectedItem(item);
    }
    EmptyState().Visibility(visible ? Visibility::Collapsed : Visibility::Visible);
    EmptyTitle().Text(m_snapshot.services.empty() ? L"还没有 NssmNext 服务" : L"没有匹配的服务");
    EmptyMessage().Text(
        m_snapshot.services.empty()
            ? L"点击右上角的“新增服务”开始。这里只显示由 NssmNext 创建的服务。"
            : L"尝试更换搜索关键词或清空搜索。");
    ListCount().Text(L"显示 " + std::to_wstring(visible) + L" / " +
                     std::to_wstring(m_snapshot.services.size()) + L" 个服务");
    UpdateControls();
}
} // namespace winrt::NssmNext::implementation
