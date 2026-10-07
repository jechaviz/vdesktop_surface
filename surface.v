module vdesktop_surface

import vdirty_regions
import vwin32

pub enum Backend {
	auto
	win32_gdi
	headless
}

pub struct SurfaceConfig {
pub:
	title string = 'V Desktop'
	width int = 1200
	height int = 800
	start_maximized bool
	borderless bool
	backend Backend = .auto
}

pub struct SurfaceContract {
pub:
	name string = 'vdesktop-surface-v1'
	backend Backend
	external_display_list bool = true
	dirty_regions bool = true
	unicode_text bool = true
	double_buffered bool = true
}

pub fn contract(config SurfaceConfig) SurfaceContract {
	backend := if config.backend == .auto {
		$if windows {
			Backend.win32_gdi
		} $else {
			Backend.headless
		}
	} else {
		config.backend
	}
	return SurfaceContract{backend: backend}
}

pub fn initial_viewport(config SurfaceConfig) vdirty_regions.Rect {
	return vdirty_regions.Rect{x: 0, y: 0, w: max_int(1, config.width), h: max_int(1, config.height)}
}

pub fn window_style(config SurfaceConfig) u32 {
	if config.borderless {
		return vwin32.borderless_style()
	}
	return vwin32.standard_frame_style()
}

pub fn ready(config SurfaceConfig, list DisplayList) bool {
	c := contract(config)
	return c.external_display_list && c.dirty_regions && list.valid()
}

fn max_int(a int, b int) int {
	return if a > b { a } else { b }
}
