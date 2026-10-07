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
