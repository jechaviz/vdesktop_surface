# Optional WebView2 backend

This directory builds the Windows compatibility backend used by `vdesktop_surface`.
The V module itself does not link WebView2 and remains usable without this backend.

The build fetches `webview/webview` at the pinned tag **0.12.0** and produces
`bin/vdesktop_webview2.dll`. The bridge embeds that engine in a child HWND, so
the host application's native chrome and agent UI remain outside the web content surface.

## Build

```powershell
.\bootstrap.ps1
```

Then either copy `bin\vdesktop_webview2.dll` beside the final executable or set:

```powershell
$env:VDESKTOP_WEBVIEW2_DLL = "<absolute-path>\vdesktop_webview2.dll"
```

The WebView2 runtime must be present on the target Windows machine. No Python or
Node runtime is introduced into the application.

## ABI v6 probe

The bridge ABI is version 6. The state probe returns URL, title, bounded visible text, actionable-control text, a bounded opaque structure payload, asynchronous action receipts, and the latest WebView2 download lifecycle event (URL, result path, MIME type, state, bytes received, and expected total). `load_count` advances only for a real document load; action-triggered refreshes and download progress do not masquerade as navigation. Consumers such as Hebrowser project these transport fields into neutral browser contracts.
