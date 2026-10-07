# vdesktop_surface

Neutral surface boundary for V desktop applications.

It owns the display-list contract, viewport/dirty-region exchange, backend selection, and native window policy. Browser- or editor-specific behavior belongs above this layer. The large branded Win32 shell can now be decomposed behind this API rather than copied into new products.
