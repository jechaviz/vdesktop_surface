module vdesktop_surface

import vdirty_regions

fn test_styled_text_uses_extended_backward_compatible_transport() {
	list := DisplayList{
		revision: 6
		viewport: vdirty_regions.Rect{0, 0, 640, 480}
		commands: [
			DisplayCommand{
				kind: .text
				rect: vdirty_regions.Rect{10, 15, 240, 40}
				text: 'Browse & act'
				value: u32(0xfff9ffff)
				font_size_px: 28
				font_weight: 700
			},
			DisplayCommand{
				kind: .text
				rect: vdirty_regions.Rect{10, 60, 200, 24}
				text: 'Legacy text'
				value: u32(0xffbbccdd)
			},
		]
	}
	assert list.valid()
	payload := encode_display_list(list)
	assert payload.contains('F|10|15|240|40|28|700|4294574079|Browse & act')
	assert payload.contains('T|10|60|200|24|')
	assert !payload.contains('F|10|60|')
}

fn test_font_sizes_and_weights_are_bounded_for_native_transport() {
	list := DisplayList{
		revision: 7
		viewport: vdirty_regions.Rect{0, 0, 640, 480}
		commands: [
			DisplayCommand{
				kind: .text
				rect: vdirty_regions.Rect{0, 0, 100, 30}
				text: 'Clamped'
				font_size_px: 900
				font_weight: 3000
			},
		]
	}
	payload := encode_display_list(list)
	assert payload.contains('F|0|0|100|30|96|900|')
}
