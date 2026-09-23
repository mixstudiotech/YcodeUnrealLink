# Ycode Unreal Link

YcodeLink is the Unreal Editor companion plugin for **[Ycode (by Mix Studio Tech)](https://mixstudio.tech/product/ycode)**. Use this plugin together with the Ycode IDE to connect your Unreal project to editor log streaming, Play-In-Editor controls, Blueprint navigation, Live Coding / Hot Reload, shader information, and AI agent tools.

The plugin exposes these capabilities through a local MCP (Streamable HTTP) endpoint inside the Unreal Editor process.

- Ycode: [Product information and downloads](https://mixstudio.tech/product/ycode)
- Repository: [mixstudiotech/YcodeUnrealLink](https://github.com/mixstudiotech/YcodeUnrealLink)
- Releases: [Versioned downloads](https://github.com/mixstudiotech/YcodeUnrealLink/releases)
- Support: [Issues](https://github.com/mixstudiotech/YcodeUnrealLink/issues)

## Requirements and compatibility

- **Ycode (by Mix Studio Tech)** installed on Windows. Get Ycode from [mixstudio.tech/product/ycode](https://mixstudio.tech/product/ycode).
- **Ycode's Unreal Engine 5.8.1 `5.8-ycode` branch**, including the HTTPServer extensions listed below.
- A Windows C++ toolchain supported by that engine version.

Version 1.0.0 is distributed as source. It does not include precompiled plugin DLLs, Unreal Engine, or the Ycode IDE.

The MCP event stream depends on these HTTPServer extensions:

- `FHttpServerResponse::StreamingBodyQueue` and `StreamingBodyComplete`.
- `EHttpServerResponseFlags::CloseAfterWrite` and its connection shutdown implementation.

Stock UE 5.7 cannot compile this version. Stock UE 5.8 compatibility cannot be assumed from the version number alone. If compilation reports missing members listed above, use an engine that includes these extensions. Removing those calls breaks event streaming and connection completion.

The plugin depends on the engine's `PythonScriptPlugin`, `EditorScriptingUtilities`, and `Niagara` plugins. `YcodeSourceCodeAccess` and `YcodeDebuggerSupport` load only on Win64; other platforms have not been validated. Epic's `ModelContextProtocol` module is not a dependency.

## Installation

Install [Ycode (by Mix Studio Tech)](https://mixstudio.tech/product/ycode), then close Unreal Editor and run the following command from your Unreal project directory:

```powershell
git clone https://github.com/mixstudiotech/YcodeUnrealLink.git Plugins/Developer/YcodeLink
```

Alternatively, use GitHub's **Code > Download ZIP**, extract it, and rename the extracted directory to `YcodeLink` under `<Project>/Plugins/Developer/`. The resulting layout must contain:

```text
<Project>/Plugins/Developer/YcodeLink/YcodeLink.uplugin
<Project>/Plugins/Developer/YcodeLink/Source/
```

Build the project's Editor target with the matching engine, open the project, and confirm that **Ycode Link** is enabled under **Edit > Plugins**. Keep only one installation of YcodeLink available to a project. For an engine-wide installation, use `<UE>/Engine/Plugins/Developer/YcodeLink`.

Open the same `.uproject` in Ycode. The IDE discovers `<Project>/Intermediate/Ycode/EditorLink.json` and connects to Unreal Editor. Ycode's Unreal panel also provides plugin installation commands.

To use Ycode as your source editor, select **Ycode** under **Editor Preferences > General > Source Code**. The plugin prefers the connected IDE instance. You can also set `YCODE_PATH` to the path of `ycode.exe` or its containing directory.

## Building

Use the matching engine's AutomationTool to build the plugin in a temporary host project. Keep the output directory separate from the source directory: AutomationTool clears an existing output directory.

```powershell
$engineRoot = 'C:/UnrealEngine-5.8-ycode'
$pluginFile = (Resolve-Path './YcodeLink.uplugin').Path
& "$engineRoot/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin `
  "-Plugin=$pluginFile" "-Package=C:/Build/YcodeLink-1.0.0-Win64" `
  -HostPlatforms=Win64 -TargetPlatforms=Win64
```

You can also build the full Editor target of a project containing the plugin. Building individual modules with `-Module=` does not produce the complete `Binaries/Win64/UnrealEditor.modules` manifest and may result in an `Incompatible or missing module` error.

## Modules

| Module | Loading phase | Capabilities |
|---|---|---|
| `YcodeLink` | PreDefault | HTTP MCP server, sessions, event streams, connection discovery, log capture, `ue_editor_info`, `ue_ide_register`, `ue_log_tail`, and `ue_epic_mcp` |
| `YcodeEditor` | Default | PIE controls and play settings, Blueprint navigation, Hot Reload / Live Coding, shader directory mappings, and permutation queries |
| `YcodeAgentTools` | Default | Python execution, asset search, screenshots, viewport camera control, actor spawning, and the Python-accessible `unreal.YcodeAgentBridgeLibrary` |
| `YcodeDebuggerSupport` | PreDefault (Win64) | Exports Blueprint VM frame inspection helpers and result buffers |
| `YcodeSourceCodeAccess` | Default (Win64) | Open in Ycode and navigate to a file, line, and column |

`ue_shader_permutations` evaluates shader compilation environments through the engine. Material/MeshMaterial queries use conservative material parameters and do not include every define produced by material translation; results report `materialEnvironment: false`. `YcodeDebuggerSupport` exports the helper interface, but IDE debugger integration for Blueprint frames remains pending.

## MCP connection

- The endpoint is `http://127.0.0.1:<port>/mcp`. Port selection starts at 46100; `-YcodeLinkPort=N` specifies the first port to try.
- Each editor session generates a new bearer token. The URL, token, PID, project, and version information are written to `<Project>/Intermediate/Ycode/EditorLink.json`. Requests must include `Authorization: Bearer <token>` or `X-Ycode-Token`. Non-loopback `Origin` values are rejected.
- HTTPServer binds to localhost by default. Keep that binding: clients with the token can execute Python and modify scenes. The connection file contains credentials and must stay out of version control.
- The connection file is removed when the endpoint stops. Clients should also check that the editor PID is still running.

| Request | Purpose |
|---|---|
| `POST /mcp` | JSON-RPC 2.0: `initialize`, `notifications/initialized`, `ping`, `tools/list`, `tools/call`, and `resources/list` (empty) |
| `GET /mcp` | With `Accept: text/event-stream`, receives logs, PIE state, play settings, Hot Reload, and Live Coding notifications |
| `DELETE /mcp` | Ends the session |

`initialize` returns the `Mcp-Session-Id` response header. Include that header in subsequent requests. Event streams send a keepalive every 15 seconds and close after approximately 4 MiB. Reconnect and use `ue_log_tail {afterSeq}` to retrieve missed log entries.

## Validation scope

Earlier integration records cover compilation of all five modules, editor loading, and MCP tool calls using UE 5.8.1 `5.8-ycode`, Win64, and the FPS test project. The initial standalone source release checked source contents, module declarations, and package contents. Compilation and runtime tests were not rerun for that release because the release machine had UE 5.7.4 checked out, without the required HTTPServer APIs.

## License

YcodeLink is released under the [MIT License](LICENSE).

This license covers the YcodeLink plugin in this repository. Ycode and Unreal Engine are separate products governed by their respective licenses.

Copyright Pix Philosophy (HK) Limited.
