module vdesktop_surface

#flag windows -luser32
#flag windows -lgdi32
#flag windows @VMODROOT/native_win32.c

$if windows {
	fn C.vdesktop_surface_open(title &u8, width int, height int, flags int, payload &u8) voidptr
	fn C.vdesktop_surface_alive(handle voidptr) int
	fn C.vdesktop_surface_poll(handle voidptr, kind &int, x &int, y &int, width &int,
		height &int, key &int, codepoint &u32) int
	fn C.vdesktop_surface_set_payload(handle voidptr, payload &u8)
	fn C.vdesktop_surface_present(handle voidptr)
	fn C.vdesktop_surface_destroy(handle voidptr)
}

pub enum EventKind {
	none
	close
	resize
	pointer_down
	pointer_move
	key_down
	text
}

pub struct SurfaceEvent {
pub:
	kind EventKind
	x int
	y int
	width int
	height int
	key int
	codepoint u32
}

pub struct NativeWindow {
pub mut:
	handle voidptr
	width int
	height int
}

pub fn open_native_window(config SurfaceConfig, list DisplayList) !NativeWindow {
	$if windows {
		payload := encode_display_list(list)
		handle := C.vdesktop_surface_open(config.title.str, config.width, config.height,
			native_window_flags(config), payload.str)
		if isnil(handle) {
			return error('failed to create native Win32 surface')
		}
		return NativeWindow{
			handle: handle
			width: config.width
			height: config.height
		}
	} $else {
		return error('native window backend is currently available on Windows only')
	}
}

pub fn (window NativeWindow) alive() bool {
	$if windows {
		if isnil(window.handle) {
			return false
		}
		return C.vdesktop_surface_alive(window.handle) != 0
	} $else {
		return false
	}
}

pub fn (mut window NativeWindow) poll() []SurfaceEvent {
	mut events := []SurfaceEvent{cap: 16}
	$if windows {
		if isnil(window.handle) {
			return events
		}
		for events.len < 128 {
			mut kind := 0
			mut x := 0
			mut y := 0
			mut width := 0
			mut height := 0
			mut key := 0
			mut codepoint := u32(0)
			if C.vdesktop_surface_poll(window.handle, &kind, &x, &y, &width, &height, &key,
				&codepoint) == 0 {
				break
			}
			event_kind := event_kind_from_native(kind)
			if event_kind == .resize {
				window.width = width
				window.height = height
			}
			events << SurfaceEvent{
				kind: event_kind
				x: x
				y: y
				width: width
				height: height
				key: key
				codepoint: codepoint
			}
		}
	}
	return events
}

pub fn (window NativeWindow) set_display_list(list DisplayList) {
	$if windows {
		if isnil(window.handle) {
			return
		}
		payload := encode_display_list(list)
		C.vdesktop_surface_set_payload(window.handle, payload.str)
	}
}

pub fn (window NativeWindow) present() {
	$if windows {
		if !isnil(window.handle) {
			C.vdesktop_surface_present(window.handle)
		}
	}
}

pub fn (mut window NativeWindow) destroy() {
	$if windows {
		if !isnil(window.handle) {
			C.vdesktop_surface_destroy(window.handle)
			window.handle = voidptr(0)
		}
	}
}

fn native_window_flags(config SurfaceConfig) int {
	mut flags := 0
	if config.start_maximized {
		flags |= 1
	}
	if config.borderless {
		flags |= 2
	}
	return flags
}

fn event_kind_from_native(kind int) EventKind {
	return match kind {
		1 { .close }
		2 { .resize }
		3 { .pointer_down }
		4 { .pointer_move }
		5 { .key_down }
		6 { .text }
		else { .none }
	}
}
