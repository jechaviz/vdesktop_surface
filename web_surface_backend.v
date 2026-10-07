module vdesktop_surface

#flag windows @VMODROOT/web_surface_backend_windows.c

$if windows {
	fn C.vdesktop_surface_web_available() int
	fn C.vdesktop_surface_web_create(parent_handle u64, x int, y int, w int, h int,
		url &u8, debug int) voidptr
	fn C.vdesktop_surface_web_navigate(handle voidptr, url &u8) int
	fn C.vdesktop_surface_web_set_bounds(handle voidptr, x int, y int, w int, h int) int
	fn C.vdesktop_surface_web_set_visible(handle voidptr, visible int) int
	fn C.vdesktop_surface_web_probe(handle voidptr, load_count &u64, url &u8, url_cap int,
		title &u8, title_cap int, text &u8, text_cap int) int
	fn C.vdesktop_surface_web_destroy(handle voidptr)
}

pub struct WebSurfaceProbe {
pub:
	ready        bool
	load_count   u64
	url          string
	title        string
	visible_text string
}

pub struct WebSurfaceHost {
pub mut:
	handle voidptr
	state  WebSurfaceState
}

pub fn web_surface_backend_available() bool {
	$if windows {
		return C.vdesktop_surface_web_available() != 0
	} $else {
		return false
	}
}

pub fn (mut host WebSurfaceHost) apply(command WebSurfaceCommand) ! {
	if !command.valid() {
		return error('invalid embedded web surface command')
	}
	if host.state.attached && host.state.id != command.id {
		return error('embedded web surface id mismatch')
	}
	match command.kind {
		.attach {
			host.attach(command)!
		}
		.navigate {
			host.require_attached(command.id)!
			$if windows {
				if C.vdesktop_surface_web_navigate(host.handle, command.url.str) == 0 {
					return error('embedded web surface navigation failed')
				}
			} $else {
				return error('embedded web surface backend is available on Windows only')
			}
			host.state = WebSurfaceState{
				...host.state
				url: command.url
				revision: command.revision
				last_error: ''
			}
		}
		.set_bounds {
			host.require_attached(command.id)!
			$if windows {
				if C.vdesktop_surface_web_set_bounds(host.handle, command.bounds.x, command.bounds.y,
					command.bounds.w, command.bounds.h) == 0 {
					return error('embedded web surface resize failed')
				}
			} $else {
				return error('embedded web surface backend is available on Windows only')
			}
			host.state = WebSurfaceState{
				...host.state
				bounds: command.bounds
				revision: command.revision
				last_error: ''
			}
		}
		.set_visible {
			host.require_attached(command.id)!
			$if windows {
				if C.vdesktop_surface_web_set_visible(host.handle, if command.visible { 1 } else { 0 }) == 0 {
					return error('embedded web surface visibility update failed')
				}
			} $else {
				return error('embedded web surface backend is available on Windows only')
			}
			host.state = WebSurfaceState{
				...host.state
				visible: command.visible
				revision: command.revision
				last_error: ''
			}
		}
		.close {
			host.close()
			host.state = WebSurfaceState{
				id: command.id
				revision: command.revision
			}
		}
	}
}

fn (mut host WebSurfaceHost) attach(command WebSurfaceCommand) ! {
	if host.state.attached {
		return error('embedded web surface is already attached')
	}
	$if windows {
		if C.vdesktop_surface_web_available() == 0 {
			return error('embedded WebView2 backend is not installed')
		}
		handle := C.vdesktop_surface_web_create(command.parent_handle, command.bounds.x,
			command.bounds.y, command.bounds.w, command.bounds.h, command.url.str, 0)
		if isnil(handle) {
			return error('embedded WebView2 backend failed to create a surface')
		}
		host.handle = handle
		host.state = WebSurfaceState{
			id: command.id
			attached: true
			visible: command.visible
			url: command.url
			bounds: command.bounds
			revision: command.revision
		}
	} $else {
		return error('embedded web surface backend is available on Windows only')
	}
}

fn (host WebSurfaceHost) require_attached(id string) ! {
	if !host.state.attached || isnil(host.handle) {
		return error('embedded web surface is not attached')
	}
	if host.state.id != id {
		return error('embedded web surface id mismatch')
	}
}

pub fn (host WebSurfaceHost) probe() WebSurfaceProbe {
	if !host.state.attached || isnil(host.handle) {
		return WebSurfaceProbe{}
	}
	$if windows {
		mut load_count := u64(0)
		mut url := []u8{len: 4096}
		mut title := []u8{len: 1024}
		mut body := []u8{len: 12001}
		ready := C.vdesktop_surface_web_probe(host.handle, &load_count, &url[0], url.len,
			&title[0], title.len, &body[0], body.len) != 0
		return WebSurfaceProbe{
			ready: ready
			load_count: load_count
			url: nul_terminated_text(url)
			title: nul_terminated_text(title)
			visible_text: nul_terminated_text(body)
		}
	} $else {
		return WebSurfaceProbe{}
	}
}

fn nul_terminated_text(buffer []u8) string {
	mut end := 0
	for end < buffer.len && buffer[end] != 0 {
		end++
	}
	if end == 0 {
		return ''
	}
	return buffer[..end].bytestr()
}

pub fn (mut host WebSurfaceHost) close() {
	$if windows {
		if !isnil(host.handle) {
			C.vdesktop_surface_web_destroy(host.handle)
		}
	}
	host.handle = voidptr(0)
	host.state = WebSurfaceState{}
}
