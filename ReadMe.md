# Ycode Unreal Link

YcodeLink 将 Unreal Editor 连接到 Ycode IDE，提供编辑器日志、Play-In-Editor 控制、Blueprint 导航、Live Coding / Hot Reload、shader 信息和 AI Agent 工具。编辑器进程通过本机 MCP（Streamable HTTP）端点提供这些能力。

- 仓库：[mixstudiotech/YcodeUnrealLink](https://github.com/mixstudiotech/YcodeUnrealLink)
- 下载：[Releases](https://github.com/mixstudiotech/YcodeUnrealLink/releases)
- 问题反馈：[Issues](https://github.com/mixstudiotech/YcodeUnrealLink/issues)

## 版本与依赖

**1.0.0 是源码发布，目标是 Ycode 的 Unreal Engine 5.8.1 `5.8-ycode` 引擎分支。** 编译还需要该引擎版本支持的 Windows C++ 工具链；源码包不包含预编译 DLL、Unreal Engine 或 Ycode IDE。

当前 MCP 事件流依赖该引擎分支的 HTTPServer 扩展：

- `FHttpServerResponse::StreamingBodyQueue` / `StreamingBodyComplete`；
- `EHttpServerResponseFlags::CloseAfterWrite` 及连接写完后关闭的实现。

因此不能直接使用标准 UE 5.7 编译，也不能仅凭版本号认定标准 UE 5.8 兼容。遇到上述成员不存在的编译错误，应使用包含这些扩展的引擎；删除相关代码会破坏事件流及连接结束语义。

插件依赖引擎自带的 `PythonScriptPlugin`、`EditorScriptingUtilities` 和 `Niagara`。`YcodeSourceCodeAccess` 与 `YcodeDebuggerSupport` 仅在 Win64 加载；其他平台尚未验证。插件不链接 Epic 的 `ModelContextProtocol` 模块，该模块不是启用 YcodeLink 的前提。

## 安装

关闭 Unreal Editor，在项目目录中执行：

```powershell
git clone https://github.com/mixstudiotech/YcodeUnrealLink.git Plugins/Developer/YcodeLink
```

也可以从 Releases 下载 `YcodeLink-1.0.0-source.zip`，将其中的 `YcodeLink` 文件夹放到 `<Project>/Plugins/Developer/`。安装后应存在：

```text
<Project>/Plugins/Developer/YcodeLink/YcodeLink.uplugin
<Project>/Plugins/Developer/YcodeLink/Source/
```

使用匹配的引擎编译项目的 Editor 目标，再打开项目，在 **Edit > Plugins** 中确认 **Ycode Link** 已启用。同一个工程只保留一份 YcodeLink，避免同时安装到项目和引擎目录。引擎级安装位置为 `<UE>/Engine/Plugins/Developer/YcodeLink`。

使用 Ycode 打开同一 `.uproject` 后，IDE 会发现 `<Project>/Intermediate/Ycode/EditorLink.json` 并连接编辑器。Ycode 的 Unreal 面板也提供插件安装入口。

要将 Ycode 设为源码编辑器，在 **Editor Preferences > General > Source Code** 中选择 **Ycode**。插件优先使用已连接的 IDE 实例，也可通过 `YCODE_PATH` 环境变量指定 `ycode.exe` 或其所在目录。

## 编译

可使用匹配引擎的 AutomationTool 在独立临时工程中构建插件。输出目录必须与源码目录分开；AutomationTool 会清理已有输出目录。

```powershell
$engineRoot = 'C:/UnrealEngine-5.8-ycode'
$pluginFile = (Resolve-Path './YcodeLink.uplugin').Path
& "$engineRoot/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin `
  "-Plugin=$pluginFile" "-Package=C:/Build/YcodeLink-1.0.0-Win64" `
  -HostPlatforms=Win64 -TargetPlatforms=Win64
```

也可在安装插件的项目中构建完整 Editor 目标。只用 `-Module=` 编译模块不会生成完整的 `Binaries/Win64/UnrealEditor.modules` 清单，编辑器可能报告 `Incompatible or missing module`。

## 模块

| 模块 | 加载阶段 | 能力 |
|---|---|---|
| `YcodeLink` | PreDefault | HTTP MCP 服务端、会话、事件流、连接发现、日志捕获、`ue_editor_info` / `ue_ide_register` / `ue_log_tail` / `ue_epic_mcp` |
| `YcodeEditor` | Default | PIE 与播放设置、Blueprint 导航、Hot Reload / Live Coding、shader 虚拟目录映射与 permutation 查询 |
| `YcodeAgentTools` | Default | Python 执行、资产搜索、截图、视口相机、Actor 生成，以及 Python 可见的 `unreal.YcodeAgentBridgeLibrary` |
| `YcodeDebuggerSupport` | PreDefault（Win64） | 导出 Blueprint VM 帧解析辅助函数与结果缓冲区 |
| `YcodeSourceCodeAccess` | Default（Win64） | Open in Ycode，打开文件并定位行列 |

`ue_shader_permutations` 通过引擎求值 shader 编译环境；Material/MeshMaterial 查询使用保守的材质参数，不包含材质翻译生成的全部 defines，结果以 `materialEnvironment: false` 标识。`YcodeDebuggerSupport` 已提供导出接口，IDE 调试器对 Blueprint 帧的消费仍待接入。

## MCP 连接

- 端点为 `http://127.0.0.1:<port>/mcp`，从 46100 起选择空闲端口；`-YcodeLinkPort=N` 可指定优先尝试的端口。
- 每次编辑器启动生成新的 bearer token，随 URL、PID、工程和版本信息写入 `<Project>/Intermediate/Ycode/EditorLink.json`。请求必须带 `Authorization: Bearer <token>` 或 `X-Ycode-Token`；非 loopback 的 `Origin` 被拒绝。
- HTTPServer 默认绑定 localhost。保留该绑定设置；持有 token 的客户端可以调用 Python 执行和场景修改工具。连接文件含凭据，不应提交到版本控制。
- 端点关闭时删除连接文件；客户端还应检查 PID 是否存活。

| 请求 | 用途 |
|---|---|
| `POST /mcp` | JSON-RPC 2.0：`initialize`、`notifications/initialized`、`ping`、`tools/list`、`tools/call`、`resources/list`（空） |
| `GET /mcp` | `Accept: text/event-stream`，接收日志、PIE、播放设置、热重载和 Live Coding 通知 |
| `DELETE /mcp` | 结束会话 |

`initialize` 返回 `Mcp-Session-Id` 响应头，后续请求携带该会话头。事件流每 15 秒发送 keepalive，累计约 4 MiB 后关闭；客户端重连并用 `ue_log_tail {afterSeq}` 补齐日志。

## 发布验证范围

原集成验证记录使用 UE 5.8.1 `5.8-ycode`、Win64 和 FPS 工程，覆盖五个模块编译、编辑器加载及 MCP 工具调用。本次独立发布核对源码、模块声明和发布包内容；发布机器当前检出 UE 5.7.4，缺少上述 HTTPServer API，未重新执行匹配引擎的编译或运行验证。

Copyright Pix Philosophy (HK) Limited.
