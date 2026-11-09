#include "pch.h"
#include "EditorPages.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace nssm_next::ui::editor_pages {
void EditorPages::BuildProcess() {
    auto &page = pages[static_cast<std::size_t>(PageId::Process)];
    priority_values = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
                       ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, REALTIME_PRIORITY_CLASS};
    priority = Choice(L"优先级", L"EditorPriority",
                      {L"低（Idle）", L"低于正常", L"正常", L"高于正常", L"高", L"实时（谨慎使用）"}, -1);
    for (std::size_t i = 0; i < priority_values.size(); ++i)
        if (priority_values[i] == original.process.priority) priority.SelectedIndex(static_cast<int>(i));
    if (priority.SelectedIndex() < 0) {
        priority_values.push_back(original.process.priority);
        priority.Items().Append(box_value(hstring(L"保留现有优先级值（" + std::to_wstring(original.process.priority) + L"）")));
        priority.SelectedIndex(static_cast<int>(priority_values.size()) - 1);
        Append(page.body, Note(L"检测到非标准优先级值；不更改此选项会原样保留。请选择受支持的优先级以替换它。"));
    }
    all_cpus = Check(L"使用全部可用处理器", L"EditorAllProcessors", original.process.affinity.empty());
    affinity = TextField(L"CPU", L"EditorAffinity", original.process.affinity, 1024);
    affinity.PlaceholderText(L"例如 0-3,6,8");
    console = Check(L"为应用程序提供控制台窗口", L"EditorConsoleWindow", original.process.console_window);
    Watch(priority); Watch(all_cpus); Watch(affinity); Watch(console);
    page.first = priority;
    const auto group = page.body;
    Append(group, Pair(FieldRow(priority, nullptr, 84), all_cpus));
    Append(group, Pair(console, FieldRow(affinity, nullptr, 72)));
    Help(priority, L"实时优先级可能影响系统及其他程序响应，通常应使用“正常”。");
    if (!original.process.affinity.empty()) {
        try { (void)ParseAffinity(original.process.affinity); }
        catch (ServiceError const &) {
            Append(group, Note(L"检测到当前解析器不支持的现有亲和性配置。未更改时会保留原值；更改后必须使用 0–63 范围内的 CPU 列表。"));
        }
    }
    Help(affinity, L"使用 0–63 范围内的编号或范围列表，例如 0-3,6。关闭“全部”后至少指定一个处理器。");
    Help(console, L"控制台不会出现在当前桌面。需要 Ctrl-C 关闭方式的程序应保留控制台。");
}
void EditorPages::BuildShutdown() {
    auto &page = pages[static_cast<std::size_t>(PageId::Shutdown)];
    stop_console = Check(L"1. 发送 Ctrl-C（控制台）", L"EditorStopConsole", original.shutdown.console);
    console_ms = TextField(L"超时（毫秒）", L"EditorStopConsoleTimeout",
                           std::to_wstring(original.shutdown.console_ms), 10);
    stop_window = Check(L"2. 发送 WM_CLOSE（窗口）", L"EditorStopWindow", original.shutdown.window);
    window_ms = TextField(L"超时（毫秒）", L"EditorStopWindowTimeout",
                          std::to_wstring(original.shutdown.window_ms), 10);
    stop_threads = Check(L"3. 发送 WM_QUIT（线程）", L"EditorStopThreads", original.shutdown.threads);
    threads_ms = TextField(L"超时（毫秒）", L"EditorStopThreadsTimeout",
                           std::to_wstring(original.shutdown.threads_ms), 10);
    stop_terminate = Check(L"4. 终止进程（TerminateProcess）", L"EditorStopTerminate", original.shutdown.terminate);
    kill_tree = Check(L"同时处理整个子进程树", L"EditorKillProcessTree", original.shutdown.kill_tree);
    Watch(stop_console); Watch(console_ms); Watch(stop_window); Watch(window_ms);
    Watch(stop_threads); Watch(threads_ms); Watch(stop_terminate); Watch(kill_tree);
    page.first = stop_console;
    for (const auto &timeout : {console_ms, window_ms, threads_ms}) {
        timeout.MaxWidth(200);
        timeout.HorizontalAlignment(HorizontalAlignment::Left);
    }
    const auto group = Group(page.body, L"关闭方式及超时");
    Append(group, Pair(stop_console, FieldRow(console_ms, nullptr, 96)));
    Append(group, Pair(stop_window, FieldRow(window_ms, nullptr, 96)));
    Append(group, Pair(stop_threads, FieldRow(threads_ms, nullptr, 96)));
    Append(group, Pair(stop_terminate, kill_tree));
    Help(stop_console, L"按顺序尝试已勾选的关闭方式，各自超时默认 1500 毫秒。");
    Help(stop_terminate, L"强制终止可能丢失未写入的数据。禁用后，不响应温和关闭请求的程序可能继续运行。");
}
void EditorPages::BuildExit() {
    auto &page = pages[static_cast<std::size_t>(PageId::Exit)];
    exit_action = Choice(L"退出动作", L"EditorExitAction",
                         {L"Restart — 重启应用程序", L"Ignore — 忽略退出，服务继续运行",
                          L"Exit — 正常停止服务", L"Suicide — 非正常终止宿主，交由系统恢复策略处理"}, -1);
    const auto action = static_cast<std::uint32_t>(original.exit.action);
    if (action <= 3) exit_action.SelectedIndex(static_cast<int>(action));
    else {
        exit_action.Items().Append(box_value(hstring(L"保留现有未知动作（" + std::to_wstring(action) + L"）")));
        exit_action.SelectedIndex(4);
    }
    restart_ms = TextField(L"重启延迟（毫秒）", L"EditorRestartDelay",
                           std::to_wstring(original.exit.restart_ms), 10);
    throttle_ms = TextField(L"节流阈值（毫秒）", L"EditorThrottleDelay",
                            std::to_wstring(original.exit.throttle_ms), 10);
    Watch(exit_action); Watch(restart_ms); Watch(throttle_ms);
    page.first = exit_action;
    for (const auto &delay : {restart_ms, throttle_ms}) {
        delay.MaxWidth(200);
        delay.HorizontalAlignment(HorizontalAlignment::Left);
    }
    const auto throttle = Group(page.body, L"节流");
    Append(throttle, throttle_ms);
    Help(throttle_ms, L"默认 1500 毫秒。在阈值内反复退出时，逐步增加重试间隔，避免快速失败循环。");
    const auto restart = Group(page.body, L"应用程序退出与重启");
    Append(restart, exit_action);
    Append(restart, restart_ms);
    Help(restart_ms, L"0 表示不额外延迟。");
    Help(exit_action, L"Ignore 可能使服务显示为运行中但没有应用进程；Suicide 的恢复取决于 Windows 服务恢复配置。");
}
void EditorPages::ReadProcess(ServiceConfiguration &result) const {
    const auto selected = priority.SelectedIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= priority_values.size())
        throw ValidationError(PageId::Process, L"EditorPriority", L"请选择进程优先级。");
    result.process.priority = priority_values[static_cast<std::size_t>(selected)];
    result.process.console_window = Checked(console);
    if (Checked(all_cpus)) result.process.affinity.clear();
    else {
        result.process.affinity = Text(affinity.Text());
        ValidateText(result.process.affinity, 1024, PageId::Process, L"EditorAffinity", L"CPU 列表", true);
        if (is_new || result.process.affinity != original.process.affinity) {
            try {
                if (!ParseAffinity(result.process.affinity))
                    throw ServiceError(L"请至少选择一个 0–63 范围内的处理器。");
            } catch (ServiceError const &error) {
                throw ValidationError(PageId::Process, L"EditorAffinity", error.message);
            }
        }
    }
    result.shutdown.console = Checked(stop_console);
    result.shutdown.window = Checked(stop_window);
    result.shutdown.threads = Checked(stop_threads);
    result.shutdown.terminate = Checked(stop_terminate);
    result.shutdown.kill_tree = Checked(kill_tree);
    // Skipping a method does not erase its timeout, including a valid draft
    // entered before disabling it. All three values are loaded from the original.
    result.shutdown.console_ms = Unsigned32(console_ms, PageId::Shutdown, L"Ctrl-C 宽限时间");
    result.shutdown.window_ms = Unsigned32(window_ms, PageId::Shutdown, L"WM_CLOSE 宽限时间");
    result.shutdown.threads_ms = Unsigned32(threads_ms, PageId::Shutdown, L"WM_QUIT 宽限时间");
    if (exit_action.SelectedIndex() >= 0 && exit_action.SelectedIndex() <= 3)
        result.exit.action = static_cast<ExitAction>(exit_action.SelectedIndex());
    else if (exit_action.SelectedIndex() != 4 || static_cast<std::uint32_t>(original.exit.action) <= 3)
        throw ValidationError(PageId::Exit, L"EditorExitAction", L"请选择默认退出动作。");
    result.exit.restart_ms = Unsigned32(restart_ms, PageId::Exit, L"重启延迟");
    result.exit.throttle_ms = Unsigned32(throttle_ms, PageId::Exit, L"节流阈值");
}
} // namespace nssm_next::ui::editor_pages
