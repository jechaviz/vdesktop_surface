# vdesktop_surface

Neutral surface boundary for V desktop applications.

It owns the display-list contract, viewport/dirty-region exchange, backend selection, and native window policy. Browser- or editor-specific behavior belongs above this layer. The large branded Win32 shell can now be decomposed behind this API rather than copied into new products.


## Embedded web compatibility

The core module stays product-neutral and has no hard WebView2 link dependency.
On Windows, `WebSurfaceHost` can load `vdesktop_webview2.dll` at runtime and
execute the same attach/navigate/bounds/visibility contract used by browser
products.

Build the optional backend with:

```powershell
.\compat\webview2\bootstrap.ps1
```

The backend is pinned to `webview/webview` 0.12.0 and hosts Edge WebView2 inside
a child HWND. Copy the resulting DLL beside the application executable or set
`VDESKTOP_WEBVIEW2_DLL` to its absolute path. Without the DLL, the native V
surface remains fully usable and web-surface attachment fails explicitly.
