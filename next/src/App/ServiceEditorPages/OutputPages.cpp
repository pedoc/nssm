#include "pch.h"
#include "EditorPages.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace nssm_next::ui::editor_pages {
namespace {
ComboBox Disposition(const wchar_t *label, const wchar_t *id, std::uint32_t existing) {
    auto choice = Choice(label, id, {}, -1);
    const auto description = existing == OPEN_ALWAYS ? L"追加（OPEN_ALWAYS）" :
                             existing == CREATE_ALWAYS ? L"覆盖（CREATE_ALWAYS）" : L"自定义／未知";
    choice.Items().Append(box_value(hstring(std::wstring(L"保留现有：") + description + L"，值 " +
                                           std::to_wstring(existing))));
    choice.Items().Append(box_value(L"追加到现有文件（OPEN_ALWAYS，4）"));
    choice.Items().Append(box_value(L"启动时覆盖现有文件（CREATE_ALWAYS，2）"));
    choice.SelectedIndex(0);
    return choice;
}
void ReadDisposition(ComboBox const &choice, StreamConfiguration &stream,
                      const wchar_t *id) {
    if (stream.path.empty()) return; // A disabled stream must not be normalized.
    switch (choice.SelectedIndex()) {
    case 0: break;
    case 1: stream.disposition = OPEN_ALWAYS; break;
    case 2: stream.disposition = CREATE_ALWAYS; break;
    default: throw ValidationError(PageId::Rotation, id, L"请选择文件打开方式。");
    }
}
} // namespace
void EditorPages::BuildIo() {
    auto &page = pages[static_cast<std::size_t>(PageId::Io)];
    const std::array<const wchar_t *, 3> labels{
        L"输入（stdin）", L"输出（stdout）", L"错误（stderr）"};
    const std::array<const wchar_t *, 3> ids{
        L"EditorStdin", L"EditorStdout", L"EditorStderr"};
    const std::array<const wchar_t *, 3> button_ids{
        L"EditorBrowseStdin", L"EditorBrowseStdout", L"EditorBrowseStderr"};
    const std::array<std::wstring, 3> paths{
        original.io.input.path, original.io.output.path, original.io.error.path};
    const auto group = page.body;
    for (std::size_t i = 0; i < streams.size(); ++i) {
        streams[i] = TextField(labels[i], ids[i], paths[i], 32766);
        Watch(streams[i]);
        Button browse;
        browse.Content(box_value(L"浏览…"));
        Id(browse, button_ids[i]);
        Browse(browse, streams[i], i == 0 ? PickerKind::Input : PickerKind::Output, PageId::Io);
        Append(group, WithButton(streams[i], browse));
    }
    timestamp = Check(L"为输出日志的每一行添加时间戳", L"EditorTimestampLogs", original.io.timestamp);
    Watch(timestamp);
    page.first = streams[0];
    Append(group, timestamp);
    Help(streams[0], L"留空表示不重定向。输入文件需已存在。");
    Help(streams[1], L"输出文件可在运行时创建。追加／覆盖方式在“日志轮转”页设置。");
    Help(streams[2], L"可与标准输出共用文件。追加／覆盖方式在“日志轮转”页设置。");
}
void EditorPages::BuildRotation() {
    auto &page = pages[static_cast<std::size_t>(PageId::Rotation)];
    stdout_disposition = Disposition(L"标准输出", L"EditorStdoutDisposition",
                                     original.io.output.disposition);
    stderr_disposition = Disposition(L"标准错误", L"EditorStderrDisposition",
                                     original.io.error.disposition);
    Watch(stdout_disposition); Watch(stderr_disposition);
    page.first = stdout_disposition;
    const auto files = Group(page.body, L"启动时的文件打开方式");
    Append(files, stdout_disposition);
    Append(files, stderr_disposition);
    Help(stdout_disposition, L"覆盖会清空现有日志。未重定向的流不修改打开方式。");
    Help(stderr_disposition, L"覆盖会清空现有日志。标准输出与标准错误分别设置。");
    if (original.io.output.disposition != original.io.error.disposition ||
        (original.io.output.disposition != OPEN_ALWAYS && original.io.output.disposition != CREATE_ALWAYS) ||
        (original.io.error.disposition != OPEN_ALWAYS && original.io.error.disposition != CREATE_ALWAYS))
        Append(files, Note(L"检测到混合或自定义文件打开方式。未明确选择新的打开方式时，将逐流原样保留，而不是统一改成追加或覆盖。"));
    rotate = Check(L"启用日志轮转", L"EditorRotateFiles", original.rotation.enabled);
    page.first = rotate;
    online = Check(L"服务运行时也允许在线轮转", L"EditorRotateOnline", original.rotation.online);
    older_than = TextField(L"文件年龄（秒）", L"EditorRotateSeconds",
                           std::to_wstring(original.rotation.older_than_seconds), 10);
    larger_than = TextField(L"文件大小（字节）", L"EditorRotateBytes",
                            std::to_wstring(original.rotation.larger_than_bytes), 20);
    rotation_delay = TextField(L"操作延迟（毫秒）", L"EditorRotateDelay",
                               std::to_wstring(original.rotation.delay_ms), 10);
    Watch(rotate); Watch(online); Watch(older_than); Watch(larger_than); Watch(rotation_delay);
    for (const auto &threshold : {older_than, larger_than, rotation_delay}) {
        threshold.MaxWidth(240);
        threshold.HorizontalAlignment(HorizontalAlignment::Left);
    }
    const auto group = Group(page.body, L"日志轮转");
    Append(group, Pair(rotate, online));
    Append(group, older_than);
    Append(group, larger_than);
    Append(group, rotation_delay);
    Help(older_than, L"0 表示不限制文件年龄。");
    Help(larger_than, L"0 表示不限制文件大小，单位为字节。");
    Help(online, L"需要已重定向的输出流。");
}
void EditorPages::ReadOutput(ServiceConfiguration &result) const {
    const std::array<StreamConfiguration *, 3> targets{
        &result.io.input, &result.io.output, &result.io.error};
    const std::array<const wchar_t *, 3> ids{L"EditorStdin", L"EditorStdout", L"EditorStderr"};
    const std::array<const wchar_t *, 3> labels{L"标准输入路径", L"标准输出路径", L"标准错误路径"};
    for (std::size_t i = 0; i < streams.size(); ++i) {
        targets[i]->path = Text(streams[i].Text());
        ValidateText(targets[i]->path, 32766, PageId::Io, ids[i], labels[i]);
        if (targets[i]->path.find_first_of(L"\r\n\"") != std::wstring::npos)
            throw ValidationError(PageId::Io, ids[i], std::wstring(labels[i]) + L"必须为不带外层引号的单行路径。");
    }
    result.io.timestamp = Checked(timestamp);
    ReadDisposition(stdout_disposition, result.io.output, L"EditorStdoutDisposition");
    ReadDisposition(stderr_disposition, result.io.error, L"EditorStderrDisposition");
    result.rotation.enabled = Checked(rotate);
    result.rotation.online = Checked(online);
    result.rotation.older_than_seconds = Unsigned32(older_than, PageId::Rotation, L"轮转年龄阈值");
    result.rotation.larger_than_bytes = Unsigned64(larger_than, PageId::Rotation, L"轮转大小阈值");
    result.rotation.delay_ms = Unsigned32(rotation_delay, PageId::Rotation, L"轮转操作延迟");
}
} // namespace nssm_next::ui::editor_pages
