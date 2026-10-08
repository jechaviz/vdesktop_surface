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

## ABI v5 probe

The bridge ABI is version 5. The state probe returns URL, title, bounded visible text, actionable-control text, and a bounded opaque structure payload. `load_count` now advances only for a real document load; action-triggered state refreshes update the probe payload without masquerading as navigation. The structure field is intentionally transport-only: consumers such as Hebrowser parse it into their own neutral browser contracts. Action receipts remain asynchronous and are returned through the same probe surface.
