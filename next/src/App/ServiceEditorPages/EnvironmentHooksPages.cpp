#include "pch.h"
#include "EditorPages.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace nssm_next::ui::editor_pages {
namespace {
constexpr std::array<const wchar_t *, 5> Events{L"Start", L"Stop", L"Exit", L"Power", L"Rotate"};
std::vector<std::wstring> ReadEnvironment(TextBox const &field, const std::wstring &loaded,
                                         const std::vector<std::wstring> &original, bool enabled,
                                         const wchar_t *id, const wchar_t *label) {
    const auto text = Text(field.Text());
    ValidateText(text, (std::max)(std::size_t{65534}, loaded.size()), PageId::Environment, id, label);
    // Disabled text remains in the window's controls, not in the persisted
    // snapshot of an absent value. The other block is entirely independent.
    if (!enabled) return {};
    // Keep an untouched enabled block verbatim, including entries unsupported
    // by the current text parser (such as hidden drive entries).
    if (text == loaded) return original;
    std::vector<std::wstring> result;
    try { result = ParseConfigurationLines(text); }
    catch (ServiceError const &error) { throw ValidationError(PageId::Environment, id, error.message); }
    result.erase(std::remove(result.begin(), result.end(), std::wstring{}), result.end());
    std::size_t units = 1;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto equal = result[i].find(L'=');
        if (equal == 0 || equal == std::wstring::npos)
            throw ValidationError(PageId::Environment, id, std::wstring(label) + L"第 " +
                std::to_wstring(i + 1) + L" 行必须为 NAME=VALUE；变量名不能为空，值可以为空。");
        if (result[i].find_first_of(L"\r\n") != std::wstring::npos)
            throw ValidationError(PageId::Environment, id, L"环境变量必须每行一个 NAME=VALUE。");
        units += result[i].size() + 1;
        if (units > 32767)
            throw ValidationError(PageId::Environment, id, std::wstring(label) + L"的环境块超过 32767 个字符（含分隔符）。");
    }
    return result;
}
std::wstring HookId(std::size_t index) {
    const auto &pair = HookPairs().at(index);
    return L"EditorHook" + pair.first + pair.second;
}
} // namespace

void EditorPages::BuildEnvironment() {
    auto &page = pages[static_cast<std::size_t>(PageId::Environment)];
    original_replacement_text = JoinConfigurationLines(original.environment.replacement);
    original_extra_text = JoinConfigurationLines(original.environment.extra);
    replacement_enabled = Check(L"替换现有环境", L"EditorEnvironmentReplacementEnabled",
                                 original.environment.replacement_present);
    replacement = TextField(L"替换环境", L"EditorEnvironmentReplacement",
                            original_replacement_text, 65534, true);
    extra_enabled = Check(L"追加环境变量", L"EditorEnvironmentExtraEnabled",
                           original.environment.extra_present);
    extra = TextField(L"追加环境", L"EditorEnvironmentExtra", original_extra_text, 65534, true);
    replacement.PlaceholderText(L"每行 NAME=VALUE");
    extra.PlaceholderText(L"每行 NAME=VALUE");
    Watch(replacement_enabled); Watch(replacement); Watch(extra_enabled); Watch(extra);
    page.first = replacement_enabled;
    const auto group = page.body;
    Append(group, replacement_enabled);
    Append(group, replacement);
    Append(group, extra_enabled);
    Append(group, extra);
    Help(replacement_enabled, L"替换可能丢失 PATH、SystemRoot 等必需变量；通常优先使用追加环境。");
    Help(replacement, L"每行 NAME=VALUE。允许空值及值中的等号，不支持注释。");
    Help(extra, L"每行 NAME=VALUE。同名变量会覆盖基础环境中的值。");
}
void EditorPages::BuildHooks() {
    auto &page = pages[static_cast<std::size_t>(PageId::Hooks)];
    hook_event = Choice(L"事件", L"EditorHookEvent", {L"Start", L"Stop", L"Exit", L"Power", L"Rotate"}, 0);
    hook_action = Choice(L"动作", L"EditorHookAction", {}, -1);
    Register(hook_event); Register(hook_action);
    hook_host = Grid();
    Id(hook_host, L"EditorHookCommands");
    for (std::size_t i = 0; i < hook_commands.size(); ++i) {
        const auto &pair = HookPairs()[i];
        const auto label = pair.first + L"/" + pair.second + L" 命令";
        const auto id = HookId(i);
        hook_commands[i] = TextField(label.c_str(), id.c_str(), original.hooks.commands[i], 32766);
        Watch(hook_commands[i]);
        Button browse;
        browse.Content(box_value(L"浏览…"));
        Id(browse, id + L"Browse");
        Browse(browse, hook_commands[i], PickerKind::Application, PageId::Hooks, true);
        const auto row = WithButton(hook_commands[i], browse);
        row.Visibility(Visibility::Collapsed);
        hook_host.Children().Append(row);
    }
    redirect_hooks = Check(L"将钩子输出重定向到服务的 stdout / stderr", L"EditorRedirectHookOutput",
                            original.hooks.redirect_output);
    Watch(redirect_hooks);
    std::weak_ptr<EditorPages> weak = shared_from_this();
    hook_event.SelectionChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock(); state && !state->selecting_hook) state->SelectHookEvent();
    });
    hook_action.SelectionChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock(); state && !state->selecting_hook) state->SelectHookAction();
    });
    page.first = hook_event;
    const auto group = page.body;
    Append(group, Pair(FieldRow(hook_event), FieldRow(hook_action, nullptr, 72)));
    Append(group, hook_host);
    Append(group, redirect_hooks);
    for (const auto &command : hook_commands)
        Help(command, L"留空禁用钩子，路径含空格时请加引号。命令以服务账户执行，仅使用可信命令。");
    Help(redirect_hooks, L"在 I/O 页配置 stdout / stderr 路径。");
    SelectHookEvent();
}
void EditorPages::SelectHookEvent() {
    const auto event = hook_event.SelectedIndex();
    if (event < 0 || static_cast<std::size_t>(event) >= Events.size()) return;
    selecting_hook = true;
    hook_action.Items().Clear();
    for (const auto &pair : HookPairs())
        if (pair.first == Events[static_cast<std::size_t>(event)])
            hook_action.Items().Append(box_value(hstring(pair.second)));
    const auto remembered = hook_action_drafts[static_cast<std::size_t>(event)];
    hook_action.SelectedIndex(remembered >= 0 && remembered < static_cast<int>(hook_action.Items().Size())
                                  ? remembered : 0);
    selecting_hook = false;
    SelectHookAction();
}
std::size_t EditorPages::CurrentHook() const {
    const auto event = hook_event.SelectedIndex();
    if (event < 0 || static_cast<std::size_t>(event) >= Events.size() || hook_action.SelectedIndex() < 0)
        return hook_commands.size();
    const auto action = unbox_value<hstring>(hook_action.SelectedItem());
    for (std::size_t i = 0; i < HookPairs().size(); ++i)
        if (HookPairs()[i].first == Events[static_cast<std::size_t>(event)] &&
            HookPairs()[i].second == Text(action)) return i;
    return hook_commands.size();
}
void EditorPages::SelectHookAction() {
    const auto event = hook_event.SelectedIndex();
    if (event >= 0 && static_cast<std::size_t>(event) < Events.size())
        hook_action_drafts[static_cast<std::size_t>(event)] = hook_action.SelectedIndex();
    const auto selected = CurrentHook();
    for (std::size_t i = 0; i < hook_commands.size(); ++i)
        hook_host.Children().GetAt(static_cast<std::uint32_t>(i)).Visibility(
            i == selected ? Visibility::Visible : Visibility::Collapsed);
}
void EditorPages::RevealHook(std::size_t index) {
    if (index >= HookPairs().size()) return;
    const auto &pair = HookPairs()[index];
    for (std::size_t event = 0; event < Events.size(); ++event) {
        if (pair.first != Events[event]) continue;
        int action = 0;
        for (std::size_t i = 0; i < index; ++i)
            if (HookPairs()[i].first == pair.first) ++action;
        hook_action_drafts[event] = action;
        selecting_hook = true;
        hook_event.SelectedIndex(static_cast<int>(event));
        selecting_hook = false;
        SelectHookEvent();
        return;
    }
}
void EditorPages::ReadEnvironmentHooks(ServiceConfiguration &result) const {
    result.environment.replacement_present = Checked(replacement_enabled);
    result.environment.extra_present = Checked(extra_enabled);
    result.environment.replacement = ReadEnvironment(replacement, original_replacement_text,
        original.environment.replacement, result.environment.replacement_present,
        L"EditorEnvironmentReplacement", L"替换环境块");
    result.environment.extra = ReadEnvironment(extra, original_extra_text,
        original.environment.extra, result.environment.extra_present,
        L"EditorEnvironmentExtra", L"追加环境块");
    for (std::size_t i = 0; i < hook_commands.size(); ++i) {
        result.hooks.commands[i] = Text(hook_commands[i].Text());
        const auto id = HookId(i);
        ValidateText(result.hooks.commands[i], 32766, PageId::Hooks, id.c_str(), L"钩子命令");
        if (result.hooks.commands[i].find_first_of(L"\r\n") != std::wstring::npos)
            throw ValidationError(PageId::Hooks, id, L"每个钩子只能填写一条单行命令；需要多步操作时请调用脚本。");
    }
    result.hooks.redirect_output = Checked(redirect_hooks);
}
} // namespace nssm_next::ui::editor_pages
