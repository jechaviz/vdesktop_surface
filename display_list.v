module vdesktop_surface

import vdirty_regions

pub enum DisplayCommandKind {
	rect
	round_rect
	stroke_rect
	line
	text
	icon
	clip
}

pub struct DisplayCommand {
pub:
	kind DisplayCommandKind
	rect vdirty_regions.Rect
	x2 int
	y2 int
	text string
	icon string
	value u32
	stroke_width int = 1
	radius int
}

pub struct DisplayList {
pub:
	revision u64
	viewport vdirty_regions.Rect
	commands []DisplayCommand
	dirty []vdirty_regions.Rect
}

pub fn (list DisplayList) valid() bool {
	if !list.viewport.valid() {
		return false
	}
	for command in list.commands {
		if command.kind in [.rect, .round_rect, .stroke_rect, .text, .icon, .clip]
			&& !command.rect.valid() {
			return false
		}
	}
	return true
}

pub fn (list DisplayList) command_count(kind DisplayCommandKind) int {
	mut count := 0
	for command in list.commands {
		if command.kind == kind {
			count++
		}
	}
	return count
}
