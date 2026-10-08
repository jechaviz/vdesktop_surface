module vdesktop_surface

#flag windows @VMODROOT/web_surface_backend_windows.c

$if windows {
	fn C.vdesktop_surface_web_available() int
	fn C.vdesktop_surface_web_create(parent_handle u64, x int, y int, w int, h int,
		url &u8, debug int) voidptr
	fn C.vdesktop_surface_web_navigate(handle voidptr, url &u8) int
	fn C.vdesktop_surface_web_set_bounds(handle voidptr, x int, y int, w int, h int) int
	fn C.vdesktop_surface_web_set_visible(handle voidptr, visible int) int
	fn C.vdesktop_surface_web_eval_action(handle voidptr, action_id &u8, script &u8) int
	fn C.vdesktop_surface_web_set_cookie(handle voidptr, name &u8, value &u8, domain &u8,
		path &u8, secure int, http_only int, same_site &u8, expires_unix i64) int
	fn C.vdesktop_surface_web_probe(handle voidptr, load_count &u64, state_count &u64,
		url &u8, url_cap int, title &u8, title_cap int, text &u8, text_cap int,
		controls &u8, controls_cap int,
		structure &u8, structure_cap int, action_count &u64, action_id &u8, action_id_cap int,
		action_ok &int, action_message &u8, action_message_cap int, download_count &u64,
		download_url &u8, download_url_cap int, download_path &u8, download_path_cap int,
		download_mime &u8, download_mime_cap int, download_state &u8, download_state_cap int,
		download_bytes &i64, download_total &i64) int
	fn C.vdesktop_surface_web_destroy(handle voidptr)
}

pub struct WebSurfaceProbe {
pub:
	ready        bool
	load_count   u64
	state_count  u64
	url          string
	title        string
	visible_text   string
	controls_text  string
	structure_text string
	action_count   u64
	action_id      string
	action_ok      bool
	action_message string
	download_count u64
	download_url string
	download_path string
	download_mime string
	download_state string
	download_bytes i64
	download_total i64
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

pub fn (host WebSurfaceHost) set_cookie(cookie WebSurfaceCookie) ! {
	host.require_attached(host.state.id)!
	if !cookie.valid() {
		return error('invalid embedded web cookie')
	}
	$if windows {
		if C.vdesktop_surface_web_set_cookie(host.handle, cookie.name.str, cookie.value.str,
			cookie.domain.str, cookie.path.str, if cookie.secure { 1 } else { 0 },
			if cookie.http_only { 1 } else { 0 }, cookie.same_site.str, cookie.expires_unix) == 0 {
			return error('embedded web cookie sync failed')
		}
	} $else {
		return error('embedded web surface backend is available on Windows only')
	}
}

pub fn (host WebSurfaceHost) set_cookies(cookies []WebSurfaceCookie) ! {
	for cookie in cookies {
		host.set_cookie(cookie)!
	}
}

pub fn (host WebSurfaceHost) eval_action(action_id string, script string) ! {
	host.require_attached(host.state.id)!
	if action_id.trim_space() == '' {
		return error('embedded web action requires action id')
	}
	if script.trim_space() == '' {
		return error('embedded web action requires script')
	}
	$if windows {
		if C.vdesktop_surface_web_eval_action(host.handle, action_id.str, script.str) == 0 {
			return error('embedded web action dispatch failed')
		}
	} $else {
		return error('embedded web surface backend is available on Windows only')
	}
}

pub fn (host WebSurfaceHost) probe() WebSurfaceProbe {
	if !host.state.attached || isnil(host.handle) {
		return WebSurfaceProbe{}
	}
	$if windows {
		mut load_count := u64(0)
		mut state_count := u64(0)
		mut url := []u8{len: 4096}
		mut title := []u8{len: 1024}
		mut body := []u8{len: 12001}
		mut controls := []u8{len: 24001}
		mut structure := []u8{len: 262145}
		mut action_count := u64(0)
		mut action_id := []u8{len: 256}
		mut action_ok := 0
		mut action_message := []u8{len: 2048}
		mut download_count := u64(0)
		mut download_url := []u8{len: 4096}
		mut download_path := []u8{len: 4096}
		mut download_mime := []u8{len: 512}
		mut download_state := []u8{len: 64}
		mut download_bytes := i64(0)
		mut download_total := i64(-1)
		ready := unsafe { C.vdesktop_surface_web_probe(host.handle, &load_count, &state_count,
			&url[0], url.len, &title[0], title.len, &body[0], body.len,
			&controls[0], controls.len,
			&structure[0], structure.len, &action_count, &action_id[0], action_id.len,
			&action_ok, &action_message[0], action_message.len, &download_count,
			&download_url[0], download_url.len, &download_path[0], download_path.len,
			&download_mime[0], download_mime.len, &download_state[0], download_state.len,
			&download_bytes, &download_total) != 0 }
		return WebSurfaceProbe{
			ready: ready
			load_count: load_count
			state_count: state_count
			url: nul_terminated_text(url)
			title: nul_terminated_text(title)
			visible_text: nul_terminated_text(body)
			controls_text: nul_terminated_text(controls)
			structure_text: nul_terminated_text(structure)
			action_count: action_count
			action_id: nul_terminated_text(action_id)
			action_ok: action_ok != 0
			action_message: nul_terminated_text(action_message)
			download_count: download_count
			download_url: nul_terminated_text(download_url)
			download_path: nul_terminated_text(download_path)
			download_mime: nul_terminated_text(download_mime)
			download_state: nul_terminated_text(download_state)
			download_bytes: download_bytes
			download_total: download_total
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
