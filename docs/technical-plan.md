# ProcQuit：WTL 10.0 进程列表与退出技术方案

日期：2026-10-12。状态：待用户确认；本次仅调研与编写方案，不修改业务代码，不实际关闭或终止任何进程。

## 1. 项目现状与问题根源

- 当前项目为 Visual Studio 原生 Win32 桌面程序模板，尚无进程列表或退出实现。
- 工程使用 v143 工具集、Unicode，已配置 Win32/x64 的 Debug/Release。
- 仓库包含 WTL10.0/Include，但工程尚未接入 WTL。
- 核心技术边界：Windows 没有对任意第三方进程都有效的通用优雅退出 API。GUI、控制台、服务和无窗口后台进程的退出协议不同。发送关闭请求成功，也不代表目标进程已经退出。

## 2. 推荐实施范围

首版枚举系统当前运行的进程，提供名称/PID/路径/窗口标题搜索、手动刷新、针对选中单个进程的 Request Exit 与 Force Kill 两个独立按钮。

列表加载或刷新只进行读取，绝不自动退出枚举到的进程。Request Exit 由用户点击触发，首版通过 WM_CLOSE 尝试关闭当前桌面可识别的普通 GUI 程序。没有支持的退出渠道时明确报告，不自动升级为强制终止。

首版不递归终止子进程、不批量终止、不停止服务、不使用远程线程注入。服务停止、已知控制台进程组和特定程序的 IPC 退出协议作为独立扩展。

建议首版运行基线为 Windows 10/11，优先验证 x64，同时保留 Win32 构建配置。编译环境需有 Visual Studio C++ ATL 组件；WTL 为头文件库，无须额外安装 WTL DLL。

## 3. 页面与交互

页面使用系统原生控件，适配窗口缩放和高 DPI。依据项目禁止中文输出的约束，新增界面文字、状态、确认提示和日志均采用英文。

```text
ProcQuit
Search: [Name / PID / Path / Window title                ] [Clear] [Refresh]
-------------------------------------------------------------------------
Name          PID      Session   Window Title   Exit Method   Status   Path
example.exe   1234     1         Example        WM_CLOSE      Running  ...
worker.exe    2345     1         -              Unsupported   Running  ...
-------------------------------------------------------------------------
Selected: example.exe | PID: 1234 | Path: ...
[Request Exit]                                         [Force Kill...]
Showing 2 of 180 processes | Last refresh: 16:20:00
```

- CEdit：搜索框，约 200 ms 防抖；文本对名称、路径、窗口标题做不区分大小写的包含匹配，纯数字输入对 PID 精确匹配。
- CListViewCtrl：报告视图、整行选择、单选、双缓冲、列排序。采用 LVS_OWNERDATA + LVN_GETDISPINFO，从可见索引数组读取数据。
- CButton：Refresh、Clear、Request Exit、Force Kill...。未选择目标、目标受保护或已有操作在进行时禁用相应操作。
- CStatic：选中进程详情、数量、最新操作结果。只在确认进程对象结束后显示 Exited。
- Force Kill... 使用确认框，包含进程名、PID 和路径，并说明可能丢失未保存数据；取消为默认选择。
- 搜索无匹配时显示 No matching processes；刷新保留搜索条件、排序及同一进程实例的选择。
- Request Exit 超过默认 5 秒等待期限后显示 Still running / Timed out，保留列表项。应用可能正在等待保存对话框、拒绝关闭或没有处理消息，不将超时解释为已退出。

## 4. WTL 接入及职责划分

采用现有 Win32 主窗口承载 WTL 子对话框页面，避免替换整个模板和删除原注释。页面采用 CDialogImpl 与 CDialogResize；父窗口 WM_SIZE 调整子页面，页面负责内部控件布局。

工程增加 WTL 头文件搜索目录、C++17、Common Controls v6 清单和必要链接依赖。初始化 CAppModule、InitCommonControlsEx，接入页面键盘消息预处理，退出时终止模块；保留现有入口和菜单行为。采用 Per-Monitor V2 DPI 配置并处理尺寸变化。

职责划分：

| 模块 | 职责 |
| --- | --- |
| ProcessPage | WTL 控件、过滤、排序、选择与状态展示 |
| ProcessEnumerator | Toolhelp 快照、路径/创建时间/会话、窗口关联 |
| ProcessController | 身份校验、关闭请求、强制终止与退出确认 |
| ProcessInfo / OperationResult | 进程数据与结构化操作结果 |

这些名称为拟新增模块标识，具体文件安排在确认后落实。UI 只在主线程更新，后台任务不访问控件对象。

## 5. 进程枚举与搜索

1. 后台使用 CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)，通过 Process32FirstW / Process32NextW 获取名称、PID、父 PID、线程数。不枚举模块或线程列表。
2. 用 PROCESS_QUERY_LIMITED_INFORMATION 打开能访问的进程，通过 QueryFullProcessImageNameW 获取完整路径、GetProcessTimes 获取创建时间、ProcessIdToSessionId 获取会话。路径缓冲区允许按需增长，避免固定 MAX_PATH 截断。
3. 无法读取详情的进程继续显示，字段标记 Unknown / Access denied。进程在枚举期间结束属于正常竞争条件，不能导致整次刷新失败。
4. 每轮只调用一次 EnumWindows，以 GetWindowThreadProcessId 建立 PID 到顶层窗口集合的映射。显示候选窗口标题，但关闭时重新枚举并核对，不依赖过期 HWND。
5. 进程实例使用 PID + 创建时间识别。无法得到创建时间时保留只读展示，并禁用破坏性操作。
6. 搜索只过滤内存快照，不因每次输入重新枚举。列表保存原始记录和可见索引，排序作用于索引。
7. 基本列表可先展示，路径等详情分批补齐；路径字段在详情尚未加载时标记 Pending，并在结果补齐后重新过滤。

快照代表采集时刻，不承诺实时无延迟。首版默认打开页面加载一次、手动刷新及操作后刷新，避免无必要轮询。

## 6. 优雅退出机制选择

| 目标类型 | 机制 | 适用条件与限制 | 首版处理 |
| --- | --- | --- | --- |
| 普通桌面 GUI | PostMessageW(hwnd, WM_CLOSE, 0, 0)，超时后再尝试 WM_SYSCOMMAND/SC_CLOSE | 应用可以保存、询问、拒绝或忽略；关窗口不一定关进程 | 支持，等待实际进程结束；超时只报告，不 Kill |
| 隐藏/托盘程序 | 已知主窗口或程序提供的 IPC | 隐藏窗口可能是内部辅助窗口；退出协议因应用而异 | 不盲目关闭隐藏窗口，报告无支持渠道 |
| Windows 服务 | ControlService(SERVICE_CONTROL_STOP) | 需 SERVICE_STOP、服务接受停止；共享宿主 PID 不能等同某个服务 | 二期独立按服务操作 |
| 控制台 | GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, groupId) | 必须共享控制台，且已知 CREATE_NEW_PROCESS_GROUP 创建的组 | 不向任意枚举 PID 发送 |
| 无窗口后台程序 | 程序自定义 IPC/命名事件等 | 需要目标应用协作，外部工具不能普遍推导退出协议 | Unsupported |

GUI 请求步骤：

1. 冻结选中目标的 PID、创建时间与展示信息；打开带 PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE 的进程句柄，核对创建时间，并检查是否已经结束。
2. 校验目标不为自身、系统禁止操作目标或关键进程；身份或关键状态无法核实时拒绝执行。
3. 重新 EnumWindows，选取属于目标进程、当前桌面可见且无 owner 的常规顶层窗口，排除子窗口、工具窗口。此筛选为保守启发式，可能漏掉特殊程序。
4. 对每个候选 HWND 在发送前再次核对所属 PID；先发送 WM_CLOSE。首轮等待未结束时，再对仍有效的候选窗口发送 WM_SYSCOMMAND/SC_CLOSE。仍存在窗口销毁/复用的极小竞争窗口，Win32 不提供把窗口消息与进程句柄原子绑定的通用机制。
5. 使用 PostMessageW 避免目标无响应阻塞调用线程，记录各窗口成功/失败。成功仅表示消息入队，UIPI 可能因目标完整性级别较高而拒绝请求。
6. 在后台等待同一进程句柄，默认总等待期限 5 秒。WAIT_OBJECT_0 才标记 Exited；WAIT_TIMEOUT 为 Still running；WAIT_FAILED 保存错误信息。WM_CLOSE 与 SC_CLOSE 共用同一总期限，不逐窗或逐阶段累加等待；任何优雅退出超时都不得自动调用 TerminateProcess。

不通过发送 WM_QUIT、执行 taskkill 或远程调用 ExitProcess 冒充优雅退出。WM_QUIT 不能保证应用清理；ExitProcess 不是外部终止任意进程的接口。

Restart Manager 虽可注册 RM_UNIQUE_PROCESS，但主要面向安装/更新期间释放资源，受会话、权限和应用支持限制；不作为首版通用退出策略，且 RmForceShutdown 明确属于强制关闭。

## 7. 强制终止与权限

- 用户点击 Force Kill... 并确认后，打开带 PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION 的句柄；再次核对创建时间和关键进程状态。
- 调用 TerminateProcess，后台等待同一进程对象结束；API 成功返回仍不能立即标记 Exited。若等待超时，显示 Termination pending。
- 仅终止所选进程实例，不递归终止进程树，不按名称批量终止。浏览器等多进程程序的其他进程可能继续运行。
- 拒绝 PID 0、System、自身和 IsProcessCritical 返回为真的进程；关键状态未知时禁用终止。IsProcessCritical 是风险检查的一部分，不代表非关键进程都可随意结束；受保护进程或不具备权限时按错误返回处理。
- 首版对识别为服务宿主、会话 0 或其他会话的目标只读展示；避免通过终止共享服务宿主破坏多个服务。服务宿主映射可用 EnumServicesStatusExW，映射不可用时将能力标记为未确认。
- 默认以普通用户权限运行，按操作申请最小访问权限，不使用 PROCESS_ALL_ACCESS，不自动启用 SeDebugPrivilege。
- 需要操作高完整性级别应用时提示权限不足，用户可自行以管理员启动工具；管理员权限仍不保证能操作所有受保护进程。
- 日志使用英文事件说明及 Win32 错误码；FormatMessageW 指定英文语言，失败时使用英文回退文本，避免系统本地化产生中文日志。

## 8. 并发与性能

- 一个受控后台工作队列执行枚举和操作，不为每个进程创建线程。同一目标操作期间禁止重复点击。
- 快照约 O(P)，全局窗口映射约 O(W)，合计约 O(P + W)，避免对每个 PID 单独 EnumWindows 导致 O(P × W)。搜索为内存线性过滤，排序约 O(P log P)。
- 基本字段不需要高权限；昂贵详情按需或分批查询，缓存按进程实例管理。查询失败也缓存到本轮结束，避免反复访问拒绝目标。
- 使用 RAII 管理快照、进程、服务管理器等句柄。用于操作的进程句柄持续持有到退出确认完成，以避免 PID 复用误伤。
- 后台结果通过线程安全队列和自定义 WM_APP 通知主线程；窗口销毁时取消任务、停止通知、回收未消费结果，避免悬空 UI 指针和结果泄漏。
- 使用刷新代次丢弃迟到的旧快照，限制同一时间仅一个刷新任务。UI 批量交换数据，双缓冲和虚拟列表减少重绘。
- 所有构建中间产物、测试辅助程序、临时脚本、日志、截图均安排在 cache 中，需配置 OutDir/IntDir 等构建输出路径；不修改或整理无关文件。

## 9. 改动范围与项目约束

用户确认后预计修改现有入口/主窗口接入点、公共头文件、资源 ID、对话框资源、工程及 filters；新增页面、枚举、控制与数据结构模块。不修改 WTL 第三方源码。

所有新增代码相关文件使用 UTF-8 with BOM。已有文件保留编码、换行和注释，若待修改文件不是要求的编码，编码调整仅限该文件。现有注释不删除、不改写；只修改本功能必需的实现与配置。

当前已新增 GUI 退出请求模块，并在现有主窗口接入基本进程列表、手动刷新和选中进程的 Request Exit。现有快捷键按钮保留；搜索、WTL 页面、托盘 UI Automation 和独立强制终止尚未实现。优雅退出不会调用 Kill 接口。

编译问题记录（2026-10-13）：主窗口 `switch` 的 `WM_CREATE` 分支内声明 `LVCOLUMNW`，未使用独立作用域导致 MSVC C2360/C2361；为该分支加块作用域。控件 ID 转为 `HMENU` 时先转换为 `INT_PTR`，消除 x64 C4312。`OutDir`/`IntDir` 增加末尾反斜杠并统一放到 `cache` 下，避免 MSB8004 和中间文件散落。

## DPI 兼容补充

主窗口与快捷键窗口统一采用 Per-Monitor V2 的 DPI 缩放辅助函数，固定布局尺寸按当前窗口 DPI 换算，并处理 WM_DPICHANGED 建议矩形后重新排版。列表、滚动区域、标题栏命中区和最小窗口尺寸同步缩放，覆盖 150% 显示缩放及跨显示器移动场景。

## 10. 验证与验收

在获准实现后，使用专门测试程序验证退出，不随意关闭用户工作程序：

- 同名不同 PID、名称/路径/窗口标题模糊搜索、PID 精确搜索、无结果、刷新保留选择与排序。
- GUI 收到 WM_CLOSE 后正常结束；显示保存提示或取消后仍运行；多窗口关闭但进程保活；无响应窗口不冻结 ProcQuit。
- 无窗口目标显示 Unsupported，Request Exit 不调用 TerminateProcess；强制终止必须有独立确认。
- 枚举后目标已退出、PID 复用、访问拒绝、自身、关键进程、服务宿主与跨会话目标。
- TerminateProcess 成功后必须验证进程对象结束，目标已退出时正确处理竞争条件。
- 搜索/刷新期间关闭本工具，检查取消、句柄和结果内存清理。
- 验证 x64 Debug/Release 和 Win32 编译；检查 Visual Studio ATL 组件、DPI/缩放、Tab 导航与英文界面文字。
- 用较多进程数据衡量枚举耗时、搜索响应和 UI 流畅度；具体指标以实现后的测量为准，不在调研阶段承诺未测性能。

## 11. 官方资料

- Toolhelp 进程快照：https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes
- 进程路径：https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew
- 顶层窗口枚举：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows
- WM_CLOSE：https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-close
- PostMessageW 与 UIPI：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew
- 强制终止及异步语义：https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess
- 关键进程检查：https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocesscritical
- 服务停止：https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice
- 控制台事件：https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent
- Restart Manager：https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmshutdown

调研依据官方 API 文档和当前工程静态检查；尚未编译 WTL 集成，也未在具体目标程序上做退出验证。
