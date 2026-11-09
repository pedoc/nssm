# 原 NSSM GUI → WinUI 功能矩阵

目标是完整迁移原 `gui.cpp:6` 的 11 页配置能力，不是精简 GUI。基础 CRUD 的源码 `23b0e1d` / CI `42488865504` 已通过 18 项真实服务观察，但不代表本表高级配置已验证。

状态应分别记录模型/存储、UI 回读保存、实际界面观察、原生运行效果；当前完整迁移仍在实施，不能将页面存在当作完成。

## 布局与交付方式（2026-11-09）

- 按用户要求，编辑器沿用旧版 NSSM 的顶部 11 个标签页、字段顺序和分组，使用 WinUI 控件样式；服务名称与创建/保存、取消放在公共底栏，不采用左侧设置导航。
- 主页采用服务表格，最后一列为启动、停止、重启、编辑、删除；保留搜索、刷新、新增工具栏，去掉右侧详情卡片。
- 本轮只做实现和必要编译，交给用户手动确认；不反复运行 UI 自动验收、截图或真实服务探针。历史自动化失败记录保留，不冒充已通过。
- 本轮正常版 `out/x64/Release/NssmNext.exe` 与内存预览版 `out/UiTests/x64/Release/NssmNext.exe` 的 Release x64 编译均通过；未启动应用、运行测试或进行界面验收，也未操作真实服务，现交用户手动确认。
- 编译成功不等于高级配置原生效果验收通过。以下功能矩阵中的待验证项目仍需独立确认。

| 页面 | 原行为/物理存储 | 新实现与验收要点 | 当前状态 |
|---|---|---|---|
| 应用程序 | Application/AppParameters/AppDirectory；支持 REG_EXPAND_SZ | 原始环境表达式保留；参数≤16382，目录留空自动；浏览路径 | 基础已验证，展开/限制改动待复验 |
| 详情 | SCM display/description/start + delayed-auto | 描述原生长度、4种启动类型、重新打开 | 基础已验证，扩展待复验 |
| 登录 | SCM 账户/Windows 管理密码 | System/LocalService/NetworkService/虚拟/自定义；空密码保持语义；安全最终提交 | 模型/账户模块/UI已接且编译通过，待用户确认/隔离验证 |
| 依赖 | SCM 多字符串，服务名及 +组名 | 新增删除、双 NUL、循环依赖错误、保持组依赖 | 模型/原生/UI已接，待验证 |
| 进程 | AppPriority DWORD常量、AppAffinity SZ列表、AppNoConsole DWORD | 6级优先级、64位亲和性、控制台反向值 | 编解码/UI已接，待验证 |
| 关闭 | AppStopMethodSkip 反向位1/2/4/8；3项默认1500ms；KillTree默认1 | 4方式独立选择，3个宽限，未知位保留 | 编解码/UI已接，待验证 |
| 退出 | AppExit子键未命名SZ；Restart/Ignore/Exit/Suicide；Delay0/Throttle1500 | 默认动作与时间，未编辑退出码规则保留 | 编解码/UI已接，待验证 |
| I/O | stdin/out/err EXPAND_SZ；平面 ShareMode/CreationDisposition/Flags 后缀 | 浏览/路径、时间戳、不同流属性保留 | 编解码/UI已接，待验证 |
| 日志轮转 | DWORD启用/在线/秒/延迟/大小高低32位 | 64位阈值不截断，stdout/stderr覆盖策略分别保留 | 编解码/UI已接，待验证 |
| 环境 | AppEnvironment与AppEnvironmentExtra均MULTI_SZ，可并存 | 独立替换/追加块，空值/嵌入等号，服务级 Environment 不丢 | 编解码/UI已接，待验证 |
| 钩子 | AppEvents\\Event\\Action EXPAND_SZ + AppRedirectHook | 8合法组合，切换不丢草稿，命令/输出重定向 | 编解码/UI已接，待验证 |

## 安全与数据保真

- 所有保存保持 NssmNext 归属、实例 GUID、ImagePath、版本和跨进程锁保护。
- 深层变更日志记录路径、值名/默认值、存在性、类型、原始字节和新建键；差异写入，不重建整个 Parameters。
- 原始未知值/子键、退出码覆盖、服务级环境及未修改的物理类型保留。格式错误须诊断，不能在无关保存中悄悄覆盖。
- 密码独立于服务快照，只通过 SCM API 最终提交；LSA 全局权限新增明确确认/报告，不盲目回滚撤销。
- 低权限账户需要宿主读执行权限及管理员预先准备的私有 NssmNext 事件源，不修改旧 NSSM 源。
- 本机只做内存 UI 写操作和真实只读浏览。真实服务验证仅限已授权的一次性 runner；新增临时系统账户测试需额外授权。

## 功能基准

`nssm.rc`、`gui.cpp:configure`、`registry.cpp:create_parameters/create_exit_action/set_hook/get_hook`、`settings.cpp:settings[]`、`account.cpp`、`service.h`。根目录源文件不修改。
