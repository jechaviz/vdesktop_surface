module vdesktop_surface

import strings

pub fn encode_display_list(list DisplayList) string {
	mut out := strings.new_builder(max_int(256, list.commands.len * 64))
	out.writeln('VDS1|${list.revision}|${list.viewport.x}|${list.viewport.y}|${list.viewport.w}|${list.viewport.h}')
	for command in list.commands {
		match command.kind {
			.rect {
				out.writeln('R|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${command.value}')
			}
			.round_rect {
				out.writeln('Q|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${command.radius}|${command.value}')
			}
			.stroke_rect {
				out.writeln('S|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${command.stroke_width}|${command.value}')
			}
			.line {
				out.writeln('L|${command.rect.x}|${command.rect.y}|${command.x2}|${command.y2}|${command.stroke_width}|${command.value}')
			}
			.text {
				if command.font_size_px > 0 {
					size := min_int_transport(96, max_int_transport(8, command.font_size_px))
					weight := min_int_transport(900, max_int_transport(100, command.font_weight))
					out.writeln('F|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${size}|${weight}|${command.value}|${escape_transport(command.text)}')
				} else {
					out.writeln('T|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${command.value}|${escape_transport(command.text)}')
				}
			}
			.icon {
				out.writeln('T|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${command.value}|${escape_transport(command.icon)}')
			}
			.image {
				out.writeln('I|${command.rect.x}|${command.rect.y}|${command.rect.w}|${command.rect.h}|${escape_transport(command.source)}')
			}
			.clip {}
		}
	}
	return out.str()
}

pub fn escape_transport(value string) string {
	mut out := strings.new_builder(value.len + 8)
	for b in value.bytes() {
		if b == `%` || b == `|` || b == `\n` || b == `\r` {
			out.write_u8(`%`)
			out.write_u8(hex_digit(b >> 4))
			out.write_u8(hex_digit(b & 0x0f))
		} else {
			out.write_u8(b)
		}
	}
	return out.str()
}

fn hex_digit(value u8) u8 {
	return if value < 10 { `0` + value } else { `A` + value - 10 }
}

fn max_int_transport(a int, b int) int {
	return if a > b { a } else { b }
}

fn min_int_transport(a int, b int) int {
	return if a < b { a } else { b }
}
