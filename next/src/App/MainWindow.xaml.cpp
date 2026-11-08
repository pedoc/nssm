#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include <microsoft.ui.xaml.window.h>
#include <exception>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace {
std::wstring StateName(std::uint32_t state) {
    switch (state) {
    case SERVICE_RUNNING:
        return L"运行中";
    case SERVICE_STOPPED:
        return L"已停止";
    case SERVICE_PAUSED:
        return L"已暂停";
    case SERVICE_START_PENDING:
        return L"启动中";
    case SERVICE_STOP_PENDING:
        return L"停止中";
    default:
        return L"状态转换中";
    }
}

void SetOriginalIcon(HWND window, int kind, int metric) {
    const auto size = GetSystemMetrics(metric);
    const auto icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, size,
                                 size, LR_SHARED);
    if (icon)
        SendMessageW(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
}
} // namespace

namespace winrt::NssmNext::implementation {
MainWindow::MainWindow() {
    InitializeComponent();
    HWND window = nullptr;
    check_hresult(m_inner.as<IWindowNative>()->get_WindowHandle(&window));
    SetOriginalIcon(window, ICON_SMALL, SM_CXSMICON);
    SetOriginalIcon(window, ICON_BIG, SM_CXICON);
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

fire_and_forget MainWindow::Refresh() {
    const auto weak = get_weak();
    const auto dispatcher = this->DispatcherQueue();
    RefreshButton().IsEnabled(false);
    StatusBar().Message(L"正在只读查询服务列表…");
    co_await resume_background();
    nssm_next::ServiceSnapshot snapshot;
    std::string error;
    try {
        snapshot = nssm_next::ReadManagedServices();
    } catch (std::exception const &exception) {
        error = exception.what();
    }
    const bool queued =
        dispatcher.TryEnqueue([weak, snapshot = std::move(snapshot), error]() mutable {
            if (auto self = weak.get()) {
                self->RefreshButton().IsEnabled(true);
                if (!error.empty()) {
                    self->StatusBar().Severity(InfoBarSeverity::Error);
                    self->StatusBar().Message(to_hstring(error));
                    return;
                }
                self->m_snapshot = std::move(snapshot);
                const auto skipped = self->m_snapshot.inaccessible_parameters;
                self->StatusBar().Severity(skipped ? InfoBarSeverity::Warning
                                                   : InfoBarSeverity::Informational);
                self->StatusBar().Message(L"已查询 " +
                                          std::to_wstring(self->m_snapshot.services.size()) +
                                          L" 个 NSSM 服务；未更改任何配置。无法读取的参数项：" +
                                          std::to_wstring(skipped));
                self->RenderServices();
            }
        });
    if (!queued)
        OutputDebugStringW(L"NSSM Next: window closed before query completed.\n");
}

void MainWindow::RenderServices() {
    if (!m_loaded || !ServiceList())
        return;
    ServiceList().Items().Clear();
    DetailTitle().Text(L"选择一个服务");
    DetailText().Text(L"服务列表直接从原生核心查询，不调用外部 nssm.exe。");
    const auto filter = SearchBox().Text();
    for (std::size_t index = 0; index < m_snapshot.services.size(); ++index) {
        const auto &service = m_snapshot.services[index];
        const auto searchable =
            service.name + L" " + service.display_name + L" " + service.application;
        if (!filter.empty() &&
            FindStringOrdinal(FIND_FROMSTART, searchable.c_str(), -1, filter.c_str(), -1, TRUE) < 0)
            continue;
        StackPanel content;
        content.Spacing(4);
        TextBlock name;
        name.Text(service.display_name);
        name.FontSize(17);
        content.Children().Append(name);
        TextBlock status;
        status.Text(StateName(service.state) + L" · " + service.name);
        status.FontSize(12);
        status.Opacity(0.7);
        content.Children().Append(status);
        ListViewItem item;
        item.Content(content);
        item.Tag(box_value(static_cast<std::uint32_t>(index)));
        ServiceList().Items().Append(item);
    }
}

void MainWindow::Selection_Changed(Windows::Foundation::IInspectable const &,
                                   SelectionChangedEventArgs const &) {
    auto item = ServiceList().SelectedItem().try_as<ListViewItem>();
    if (!item)
        return;
    const auto index = unbox_value<std::uint32_t>(item.Tag());
    if (index >= m_snapshot.services.size())
        return;
    const auto &service = m_snapshot.services[index];
    DetailTitle().Text(service.display_name);
    DetailText().Text(L"服务名\n" + service.name + L"\n\n状态\n" + StateName(service.state) +
                      L"\n\n进程 PID\n" + std::to_wstring(service.process_id) + L"\n\n程序路径\n" +
                      service.application);
}
} // namespace winrt::NssmNext::implementation
