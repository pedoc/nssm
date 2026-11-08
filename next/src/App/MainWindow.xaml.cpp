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
                self->StatusBar().Message(L"正在完成服务操作，请稍候再关闭窗口。");
            }
        }
    });
    if (m_manager->IsTestBackend()) {
        window.Title(L"NSSM Next — 服务管理 [内存测试]");
        ModeBar().Title(L"内存测试模式");
        ModeBar().Message(L"所有操作仅修改当前进程内的测试数据，不会创建、修改或控制系统服务。");
        ElevateButton().Visibility(Visibility::Collapsed);
    } else if (m_manager->CanWrite()) {
        ModeBar().Title(L"管理员模式");
        ModeBar().Message(L"仅管理带有 NssmNext 归属标识的服务。新增和保存需要你明确确认。");
        ElevateButton().Visibility(Visibility::Collapsed);
    } else {
        ModeBar().Title(L"查看模式");
        ModeBar().Severity(InfoBarSeverity::Warning);
        ModeBar().Message(L"可以查看服务；新增、编辑、删除和启停操作会请求管理员权限。");
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
    RenderDetails();
}
std::optional<nssm_next::ServiceInfo> MainWindow::Selected() {
    const auto selected = ServiceList().SelectedItem().try_as<ListViewItem>();
    if (!selected)
        return std::nullopt;
    const std::wstring key(unbox_value<hstring>(selected.Tag()));
    for (const auto &service : m_snapshot.services)
        if (Key(service) == key)
            return service;
    return std::nullopt;
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
    EmptyNewButton().IsEnabled(available);
    ElevateButton().IsEnabled(available);
    const auto selected = Selected();
    const bool enabled = selected.has_value() && available;
    EditButton().IsEnabled(enabled);
    DeleteButton().IsEnabled(enabled);
    StartButton().IsEnabled(enabled && selected->state == ServiceState::Stopped &&
                            selected->startup != nssm_next::StartupMode::Disabled);
    StopButton().IsEnabled(enabled && (selected->state == ServiceState::Running ||
                                       selected->state == ServiceState::Paused));
    RestartButton().IsEnabled(enabled && (selected->state == ServiceState::Running ||
                                          selected->state == ServiceState::Paused));
}
void MainWindow::ShowError(const std::wstring &message) {
    if (m_closed)
        return;
    StatusBar().Severity(InfoBarSeverity::Error);
    StatusBar().Message(message);
}
fire_and_forget MainWindow::Refresh(std::wstring message) {
    if (!m_loaded || m_busy || m_dialog_open || m_closed)
        co_return;
    const auto weak = get_weak();
    const auto dispatcher = DispatcherQueue();
    const auto manager = m_manager;
    SetBusy(true);
    StatusBar().Severity(InfoBarSeverity::Informational);
    StatusBar().Message(L"正在读取 NssmNext 服务列表…");
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
                    self->ShowError(error);
                    return;
                }
                self->m_snapshot = std::move(snapshot);
                self->RenderServices();
                const auto skipped = self->m_snapshot.inaccessible_parameters;
                self->StatusBar().Severity(skipped ? InfoBarSeverity::Warning
                                                   : InfoBarSeverity::Informational);
                auto text = L"已查询 " + std::to_wstring(self->m_snapshot.services.size()) +
                            L" 个 NssmNext 服务。";
                if (skipped)
                    text += L" 无法验证或读取的参数项：" + std::to_wstring(skipped) + L"。";
                if (!message.empty())
                    text += L" " + message;
                self->StatusBar().Message(text);
            }
        });
}
void MainWindow::RenderServices() {
    if (!m_loaded || m_closed)
        return;
    const auto wanted = m_selected;
    ServiceList().Items().Clear();
    const auto filter = SearchBox().Text();
    std::size_t visible = 0;
    for (const auto &service : m_snapshot.services) {
        const auto searchable = service.name + L" " + service.display_name + L" " +
                                service.application + L" " + service.description;
        if (!filter.empty() &&
            FindStringOrdinal(FIND_FROMSTART, searchable.c_str(), -1, filter.c_str(), -1, TRUE) < 0)
            continue;
        StackPanel content;
        content.Spacing(4);
        content.Padding(ThicknessHelper::FromUniformLength(4));
        TextBlock name;
        name.Text(service.display_name);
        name.FontSize(17);
        content.Children().Append(name);
        TextBlock state;
        state.Text(StateName(service.state) + L" · " + service.name);
        state.FontSize(12);
        state.Opacity(0.75);
        content.Children().Append(state);
        TextBlock path;
        path.Text(service.application);
        path.TextTrimming(TextTrimming::CharacterEllipsis);
        path.FontSize(12);
        path.Opacity(0.65);
        content.Children().Append(path);
        ListViewItem item;
        item.Content(content);
        item.Tag(box_value(hstring(Key(service))));
        Automation::AutomationProperties::SetName(
            item, service.display_name + L" · " + service.name + L" · " + StateName(service.state));
        ServiceList().Items().Append(item);
        ++visible;
        if (Key(service) == wanted)
            ServiceList().SelectedItem(item);
    }
    EmptyState().Visibility(visible ? Visibility::Collapsed : Visibility::Visible);
    EmptyNewButton().Visibility(m_snapshot.services.empty() ? Visibility::Visible
                                                            : Visibility::Collapsed);
    EmptyTitle().Text(m_snapshot.services.empty() ? L"还没有 NssmNext 服务" : L"没有匹配的服务");
    EmptyMessage().Text(
        m_snapshot.services.empty()
            ? L"点击新增服务，选择要托管的程序。其他 Windows 和旧 NSSM 服务不会混入列表。"
            : L"尝试更换搜索关键词或清空搜索。");
    ListCount().Text(L"显示 " + std::to_wstring(visible) + L" / " +
                     std::to_wstring(m_snapshot.services.size()) + L" 个服务");
    RenderDetails();
}
void MainWindow::RenderDetails() {
    if (!m_loaded || m_closed)
        return;
    const auto selected = Selected();
    if (!selected) {
        DetailTitle().Text(L"选择一个服务");
        DetailStatus().Text(L"在此查看配置、编辑和控制服务。");
        DetailText().Text(L"新增、编辑和删除只会作用于带有 NssmNext "
                          L"归属标识的服务。删除不会移除应用程序或日志文件。");
    } else {
        const auto &s = *selected;
        DetailTitle().Text(s.display_name);
        DetailStatus().Text(StateName(s.state) + L" · " + s.name);
        DetailText().Text(L"应用程序\n" + s.application + L"\n\n启动参数\n" +
                          (s.arguments.empty() ? L"（无）" : s.arguments) + L"\n\n工作目录\n" +
                          s.working_directory + L"\n\n启动类型\n" + StartupName(s.startup) +
                          L"\n\n运行账户\n" + s.account + L"\n\n宿主进程 PID\n" +
                          std::to_wstring(s.process_id) + L"\n\n服务描述\n" +
                          (s.description.empty() ? L"（无）" : s.description) +
                          L"\n\n原生服务宿主\n" + s.binary_path);
    }
    UpdateControls();
}
} // namespace winrt::NssmNext::implementation
