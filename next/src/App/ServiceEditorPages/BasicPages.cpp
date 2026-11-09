#include "pch.h"
#include "EditorPages.h"
#include "../../Core/ServicePolicy.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace nssm_next::ui::editor_pages {
namespace {
Button BrowseButton(const wchar_t *id) {
    Button button;
    button.Content(box_value(L"浏览…"));
    Id(button, id);
    return button;
}
void ValidateDependency(const std::wstring &value, const std::wstring &service) {
    ValidateText(value, 257, PageId::Dependencies, L"EditorDependencyName", L"依赖名称", true);
    if (value.find_first_of(L"\r\n") != std::wstring::npos)
        throw ValidationError(PageId::Dependencies, L"EditorDependencyName", L"依赖名称必须为单行。");
    if (value.front() == L'+') {
        if (value.size() == 1)
            throw ValidationError(PageId::Dependencies, L"EditorDependencyName", L"请填写 + 后面的加载顺序组名称。");
    } else {
        try { ValidateServiceName(value); }
        catch (ServiceError const &error) {
            throw ValidationError(PageId::Dependencies, L"EditorDependencyName", error.message);
        }
        if (SameAccount(value, service))
            throw ValidationError(PageId::Dependencies, L"EditorDependencyName", L"服务不能依赖自身。");
    }
}
} // namespace

void EditorPages::BuildApplication() {
    auto &page = pages[static_cast<std::size_t>(PageId::Application)];
    name = TextField(L"服务名称", L"EditorServiceName", original.name, 256);
    name.IsReadOnly(!is_new);
    application = TextField(L"可执行文件", L"EditorApplication", original.application, 32766);
    directory = TextField(L"启动目录", L"EditorDirectory", original.working_directory, 32755);
    arguments = TextField(L"启动参数", L"EditorArguments", original.arguments, 16382, true);
    arguments.MinHeight(72);
    page.first = application;
    Watch(name); Watch(application); Watch(directory); Watch(arguments);
    suggest_name = is_new && original.name.empty();
    suggest_directory = original.working_directory.empty();
    std::weak_ptr<EditorPages> weak = shared_from_this();
    name.TextChanged([weak](auto const &, auto const &) {
        if (const auto state = weak.lock())
            if (Text(state->name.Text()) != state->suggested_name) state->suggest_name = false;
    });
    directory.TextChanged([weak](auto const &, auto const &) {
        if (const auto state = weak.lock())
            if (Text(state->directory.Text()) != state->suggested_directory) state->suggest_directory = false;
    });
    application.LostFocus([weak](auto const &, auto const &) {
        if (const auto state = weak.lock()) state->UpdateApplicationDefaults();
    });
    const auto group = page.body;
    // Service name is hosted by the common footer. This page still creates and
    // watches it so validation, dirty state and virtual-account naming agree.
    const auto browse = BrowseButton(L"EditorBrowseApplication");
    Browse(browse, application, PickerKind::Application, PageId::Application);
    Append(group, WithButton(application, browse));
    const auto folder = BrowseButton(L"EditorBrowseDirectory");
    Browse(folder, directory, PickerKind::Directory, PageId::Application);
    Append(group, WithButton(directory, folder));
    Append(group, arguments);
    directory.PlaceholderText(L"默认使用程序所在目录");
    Help(directory, L"留空时使用程序所在目录。");
    Help(arguments, L"按应用程序的要求填写命令行参数，不包含程序路径。");
}
void EditorPages::UpdateApplicationDefaults() {
    // Recheck the current values as TextChanged can be delivered after a picker
    // or focus transition. Explicit edits, including clearing a field, win.
    if (Text(name.Text()) != suggested_name) suggest_name = false;
    if (Text(directory.Text()) != suggested_directory) suggest_directory = false;
    if (!suggest_name && !suggest_directory) return;
    const auto path = Text(application.Text());
    if (path.find_first_of(L"\"\r\n") != std::wstring::npos) return;
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash + 1 == path.size()) return;
    const auto parentLength = slash == 0 || (slash == 2 && path[1] == L':') ? slash + 1 : slash;
    const auto parent = path.substr(0, parentLength);
    auto candidate = path.substr(slash + 1);
    const auto extension = candidate.find_last_of(L'.');
    if (extension != std::wstring::npos && extension > 0) candidate.resize(extension);
    if (suggest_directory && parent.size() <= static_cast<std::size_t>(directory.MaxLength())) {
        suggested_directory = parent;
        directory.Text(suggested_directory);
    }
    if (suggest_name) {
        try { ValidateServiceName(candidate); }
        catch (ServiceError const &) { return; }
        suggested_name = std::move(candidate);
        name.Text(suggested_name);
    }
}
void EditorPages::BuildDetails() {
    auto &page = pages[static_cast<std::size_t>(PageId::Details)];
    display_name = TextField(L"显示名称", L"EditorDisplayName", original.display_name, 256);
    display_name.PlaceholderText(L"留空使用服务名称");
    description = TextField(L"描述", L"EditorDescription", original.description, 16382, true);
    startup = Choice(L"启动类型", L"EditorStartup", {L"自动", L"自动（延迟启动）", L"手动", L"禁用"}, -1);
    const auto mode = static_cast<std::uint32_t>(original.startup);
    if (mode <= 3) startup.SelectedIndex(static_cast<int>(mode));
    else {
        startup.Items().Append(box_value(hstring(L"保留现有未知启动类型（" + std::to_wstring(mode) + L"）")));
        startup.SelectedIndex(4);
    }
    Watch(display_name); Watch(description); Watch(startup);
    page.first = display_name;
    startup.MaxWidth(320);
    startup.HorizontalAlignment(HorizontalAlignment::Left);
    const auto group = page.body;
    Append(group, display_name);
    Append(group, description);
    Append(group, startup);
}
void EditorPages::BuildLogon() {
    auto &page = pages[static_cast<std::size_t>(PageId::Logon)];
    account_kind = Choice(L"登录账户", L"EditorAccountKind",
        {L"LocalSystem（本地系统，高权限）", L"LocalService（本地服务）",
         L"NetworkService（网络服务）", L"虚拟服务账户（NT SERVICE\\服务名）",
         L"自定义账户"}, static_cast<int>(original.login.kind));
    username = TextField(L"账户名称", L"EditorAccountName", original.login.username, 256);
    username.PlaceholderText(L"DOMAIN\\User 或 .\\User");
    password = PasswordBox();
    password.Header(box_value(L"密码"));
    password.MaxLength(256);
    password.PasswordRevealMode(PasswordRevealMode::Hidden);
    Id(password, L"EditorPassword");
    confirmation = PasswordBox();
    confirmation.Header(box_value(L"确认密码"));
    confirmation.MaxLength(256);
    confirmation.PasswordRevealMode(PasswordRevealMode::Hidden);
    Id(confirmation, L"EditorPasswordConfirmation");
    grant_logon = Check(L"允许在需要时为此自定义账户授予“作为服务登录”权限",
                        L"EditorGrantServiceLogon", false);
    password_notice = Note(L"");
    Id(password_notice, L"EditorPasswordNotice");
    page.first = account_kind;
    Watch(account_kind); Watch(username); Watch(password); Watch(confirmation); Watch(grant_logon);
    const auto group = page.body;
    Append(group, account_kind);
    Append(group, username);
    Append(group, Pair(FieldRow(password), FieldRow(confirmation, nullptr, 72)));
    Append(group, password_notice);
    Append(group, grant_logon);
    Help(account_kind, L"优先使用满足需求的低权限账户。虚拟账户由 Windows 管理，名称随服务名确定。");
    Help(password, L"编辑同一账户时留空可保留原密码；创建服务或更换自定义账户时须填写密码。");
    Help(grant_logon, L"这会修改账户级安全策略。保存失败后新增权限也可能保留；未勾选时不会授予或撤销权限。");
}
void EditorPages::BuildDependencies() {
    auto &page = pages[static_cast<std::size_t>(PageId::Dependencies)];
    dependency_draft = original.dependencies;
    dependency_entry = TextField(L"服务或组名称", L"EditorDependencyName", L"", 257);
    dependency_group = Check(L"按加载顺序组添加（自动加 + 前缀）", L"EditorDependencyGroup", false);
    dependency_list = ListView();
    dependency_list.SelectionMode(ListViewSelectionMode::Single);
    dependency_list.MinHeight(120);
    dependency_list.MaxHeight(260);
    Id(dependency_list, L"EditorDependencies");
    Automation::AutomationProperties::SetName(dependency_list, L"服务和组依赖");
    dependency_remove = Button();
    dependency_remove.Content(box_value(L"移除选中的依赖"));
    Id(dependency_remove, L"EditorRemoveDependency");
    Button add;
    add.Content(box_value(L"添加"));
    Id(add, L"EditorAddDependency");
    Watch(dependency_entry);
    Register(dependency_list);
    std::weak_ptr<EditorPages> weak = shared_from_this();
    snapshots.push_back([weak] {
        std::wstring result;
        if (auto state = weak.lock())
            for (const auto &entry : state->dependency_draft)
                result += std::to_wstring(entry.size()) + L":" + entry;
        return result;
    });
    add.Click([weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->AddDependency();
    });
    dependency_remove.Click([weak](auto const &, auto const &) {
        if (auto state = weak.lock()) state->RemoveDependency();
    });
    dependency_list.SelectionChanged([weak](auto const &, auto const &) {
        if (auto state = weak.lock(); state && state->initialized) state->UpdateEnabled();
    });
    page.first = dependency_entry;
    const auto group = Group(page.body, L"此服务依赖以下系统组件");
    Append(group, WithButton(dependency_entry, add));
    Append(group, dependency_group);
    Append(group, dependency_list);
    Append(group, dependency_remove);
    Help(dependency_entry, L"填写内部服务名，而非显示名称。组依赖以 + 开头。");
    RefreshDependencies();
}
void EditorPages::RefreshDependencies() {
    const auto selected = dependency_list.SelectedIndex();
    dependency_list.Items().Clear();
    for (const auto &entry : dependency_draft)
        dependency_list.Items().Append(box_value(hstring(entry.data(), static_cast<std::uint32_t>(entry.size()))));
    if (!dependency_draft.empty() && selected >= 0)
        dependency_list.SelectedIndex((std::min)(selected, static_cast<int>(dependency_draft.size()) - 1));
}
void EditorPages::AddDependency() {
    try {
        auto value = Text(dependency_entry.Text());
        if (Checked(dependency_group) && !value.empty() && value.front() != L'+')
            value.insert(value.begin(), L'+');
        ValidateDependency(value, Text(name.Text()));
        for (const auto &entry : dependency_draft)
            if (SameAccount(entry, value))
                throw ValidationError(PageId::Dependencies, L"EditorDependencyName", L"该依赖已存在。");
        dependency_draft.push_back(std::move(value));
        dependency_entry.Text(L"");
        RefreshDependencies();
        dependency_list.SelectedIndex(static_cast<int>(dependency_draft.size()) - 1);
        OnChanged();
    } catch (ServiceError const &error) {
        if (show_error) show_error(error.message, PageKey(PageId::Dependencies));
        FocusField(L"EditorDependencyName");
    }
}
void EditorPages::RemoveDependency() {
    const auto selected = dependency_list.SelectedIndex();
    if (selected >= 0 && static_cast<std::size_t>(selected) < dependency_draft.size()) {
        dependency_draft.erase(dependency_draft.begin() + selected);
        RefreshDependencies();
        OnChanged();
    }
}
void EditorPages::ReadBasic(ServiceConfiguration &result) const {
    result.name = Text(name.Text());
    result.display_name = Text(display_name.Text());
    result.application = Text(application.Text());
    result.arguments = Text(arguments.Text());
    result.working_directory = Text(directory.Text());
    result.description = Text(description.Text());
    try { ValidateServiceName(result.name); }
    catch (ServiceError const &error) {
        throw ValidationError(PageId::Application, L"EditorServiceName", error.message);
    }
    ValidateText(result.display_name, 256, PageId::Details, L"EditorDisplayName", L"显示名称");
    ValidateText(result.application, 32766, PageId::Application, L"EditorApplication", L"应用程序路径", true);
    ValidateText(result.arguments, 16382, PageId::Application, L"EditorArguments", L"启动参数");
    ValidateText(result.working_directory, 32755, PageId::Application, L"EditorDirectory", L"工作目录");
    ValidateText(result.description, 16382, PageId::Details, L"EditorDescription", L"服务描述");
    if (result.application.size() + result.arguments.size() + 3 > 32766)
        throw ValidationError(PageId::Application, L"EditorArguments", L"应用程序路径与参数的完整命令行超过 Windows 长度限制。");
    if (startup.SelectedIndex() >= 0 && startup.SelectedIndex() <= 3)
        result.startup = static_cast<StartupMode>(startup.SelectedIndex());
    else if (startup.SelectedIndex() != 4 || static_cast<std::uint32_t>(original.startup) <= 3)
        throw ValidationError(PageId::Details, L"EditorStartup", L"请选择启动类型。");
}
void EditorPages::ReadLoginDependencies(ServiceConfiguration &result) const {
    const auto kind = account_kind.SelectedIndex();
    if (kind < 0 || kind > 4)
        throw ValidationError(PageId::Logon, L"EditorAccountKind", L"请选择运行账户类型。");
    result.login.kind = static_cast<AccountKind>(kind);
    if (result.login.kind == AccountKind::Custom) {
        result.login.username = Text(username.Text());
        ValidateText(result.login.username, 256, PageId::Logon, L"EditorAccountName", L"账户名称", true);
        if (result.login.username.find_first_of(L"\r\n") != std::wstring::npos)
            throw ValidationError(PageId::Logon, L"EditorAccountName", L"账户名称必须为单行。");
    } else if (result.login.kind != original.login.kind) {
        result.login.username.clear();
    }
    if (!dependency_entry.Text().empty())
        throw ValidationError(PageId::Dependencies, L"EditorDependencyName", L"依赖输入框尚有未添加的名称。请点击“添加”，或清空后再保存。");
    result.dependencies = dependency_draft;
    if (is_new || result.dependencies != original.dependencies) {
        for (std::size_t i = 0; i < result.dependencies.size(); ++i) {
            ValidateDependency(result.dependencies[i], result.name);
            for (std::size_t j = 0; j < i; ++j)
                if (SameAccount(result.dependencies[i], result.dependencies[j]))
                    throw ValidationError(PageId::Dependencies, L"EditorDependencies", L"依赖列表包含重复名称。");
        }
    }
}
void EditorPages::ValidateCredentials() const {
    if (account_kind.SelectedIndex() != static_cast<int>(AccountKind::Custom)) return;
    const auto first = password.Password();
    const auto second = confirmation.Password();
    const std::wstring_view left(first.c_str(), first.size()), right(second.c_str(), second.size());
    if (left.find(L'\0') != std::wstring_view::npos || right.find(L'\0') != std::wstring_view::npos)
        throw ValidationError(PageId::Logon, L"EditorPassword", L"密码不能包含空字符（NUL）。");
    if (left != right)
        throw ValidationError(PageId::Logon, L"EditorPasswordConfirmation", L"密码与确认密码不一致。请重新确认。");
    const bool same = !is_new && original.login.kind == AccountKind::Custom &&
                      SameAccount(original.login.username, Text(username.Text()));
    if (left.empty() && !same)
        throw ValidationError(PageId::Logon, L"EditorPassword",
                              L"创建服务或更换自定义账户时，请输入并确认该账户的密码。");
}
CredentialRequest EditorPages::TakeCredentials() {
    ValidateCredentials();
    CredentialRequest result;
    if (account_kind.SelectedIndex() == static_cast<int>(AccountKind::Custom)) {
        const auto value = password.Password();
        if (!value.empty()) {
            result.password.Assign(value.c_str(), value.size());
            result.password_supplied = true;
            credentials_extracted = true;
        }
        result.grant_service_logon = Checked(grant_logon);
    }
    ClearSecrets();
    OnChanged();
    return result;
}
void EditorPages::ClearSecrets() {
    const bool was_initialized = initialized;
    initialized = false;
    if (password) password.Password(L"");
    if (confirmation) confirmation.Password(L"");
    initialized = was_initialized;
}
} // namespace nssm_next::ui::editor_pages
