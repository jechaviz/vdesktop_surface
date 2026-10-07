module vdesktop_surface

import vdirty_regions

pub enum WebSurfaceCommandKind {
	attach
	navigate
	set_bounds
	set_visible
	close
}

pub struct WebSurfaceCommand {
pub:
	kind          WebSurfaceCommandKind
	id            string
	parent_handle u64
	url           string
	bounds        vdirty_regions.Rect
	visible       bool
	revision      u64
}

pub struct WebSurfaceState {
pub:
	id        string
	attached  bool
	visible   bool
	url       string
	bounds    vdirty_regions.Rect
	revision  u64
	last_error string
}

pub fn attach_web_surface(id string, parent_handle u64, url string,
	bounds vdirty_regions.Rect, revision u64) WebSurfaceCommand {
	return WebSurfaceCommand{
		kind: .attach
		id: id
		parent_handle: parent_handle
		url: url
		bounds: bounds
		visible: true
		revision: revision
	}
}

pub fn navigate_web_surface(id string, url string, revision u64) WebSurfaceCommand {
	return WebSurfaceCommand{
		kind: .navigate
		id: id
		url: url
		revision: revision
	}
}

pub fn resize_web_surface(id string, bounds vdirty_regions.Rect, revision u64) WebSurfaceCommand {
	return WebSurfaceCommand{
		kind: .set_bounds
		id: id
		bounds: bounds
		revision: revision
	}
}

pub fn show_web_surface(id string, visible bool, revision u64) WebSurfaceCommand {
	return WebSurfaceCommand{
		kind: .set_visible
		id: id
		visible: visible
		revision: revision
	}
}

pub fn close_web_surface(id string, revision u64) WebSurfaceCommand {
	return WebSurfaceCommand{
		kind: .close
		id: id
		revision: revision
	}
}

pub fn (command WebSurfaceCommand) valid() bool {
	if command.id.trim_space() == '' {
		return false
	}
	return match command.kind {
		.attach {
			command.parent_handle != 0 && command.url.trim_space() != '' && command.bounds.valid()
		}
		.navigate {
			command.url.trim_space() != ''
		}
		.set_bounds {
			command.bounds.valid()
		}
		.set_visible, .close {
			true
		}
	}
}
