module vdesktop_surface

import vdirty_regions

fn test_display_list_contract() {
	config := SurfaceConfig{width: 640, height: 480}
	list := DisplayList{
		revision: 1
		viewport: initial_viewport(config)
		commands: [
			DisplayCommand{
				kind: .rect
				rect: vdirty_regions.Rect{0, 0, 100, 100}
			},
		]
	}
	assert list.valid()
	assert ready(config, list)
	assert list.command_count(.rect) == 1
}


fn test_embedded_web_surface_contracts() {
	attach := attach_web_surface('main', 42, 'https://example.com',
		vdirty_regions.Rect{x: 10, y: 20, w: 800, h: 600}, 1)
	assert attach.valid()
	assert attach.kind == .attach
	assert navigate_web_surface('main', 'https://example.org', 2).valid()
	assert resize_web_surface('main', vdirty_regions.Rect{x: 0, y: 0, w: 640, h: 480}, 3).valid()
	assert show_web_surface('main', false, 4).valid()
	assert close_web_surface('main', 5).valid()
}
