// view_doc_hex.h — Hex document view: offset, hex bytes, and ASCII columns

#pragma once

#include "view_doc_readonly.h"

class hex_doc_view final : public read_only_doc_view
{
	static constexpr int bytes_per_line = 16;

	// Column layout (in characters):
	//   offset: 8 hex digits + 2 spaces = 10 chars
	//   hex:    16 * 3 + 1 (gap at byte 8) = 49 chars  + 1 trailing space = 50
	//   ascii:  "|" + 16 chars + "|" = 18 chars
	// Total: ~78 chars
	static constexpr int offset_chars = 10;
	static constexpr int hex_chars = 50;

	// Reused across lines and paints so drawing allocates nothing
	mutable std::string _line_buf;
	mutable std::string _offset_buf;
	mutable std::string _hex_buf;
	mutable std::string _ascii_buf;

	static void append_hex_byte(std::string& out, const uint8_t value)
	{
		static constexpr char digits[] = "0123456789ABCDEF";
		out += digits[value >> 4];
		out += digits[value & 0x0F];
	}

public:
	hex_doc_view(app_events& events) : read_only_doc_view(events)
	{
	}

	~hex_doc_view() override = default;

protected:
	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto& styles = _events.styles();
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();
		const auto cx = _font_extent.cx;
		const auto cy = _font_extent.cy;

		const auto bg = style_to_color(style::normal_bkgnd);
		const auto offset_color = style_to_color(style::code_number);
		const auto hex_color = style_to_color(style::code_keyword);
		const auto ascii_color = style_to_color(style::code_string);
		const auto separator_color = style_to_color(style::code_comment);

		draw.fill_solid_rect(rcClient, bg);

		constexpr int content_chars = offset_chars + hex_chars + 1 + bytes_per_line + 1;
		const auto content_width = content_chars * cx;
		const auto left_margin = std::max(cx, (rcClient.right - content_width) / 2);

		auto nCurrentLine = std::max(0, cy > 0 ? (_scroll_offset.y - cy) / cy : 0);
		auto y = line_content_offset(nCurrentLine) - _scroll_offset.y + pad_top;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto& line = (*_doc)[nCurrentLine];
			line.render(_line_buf);

			const auto num_bytes = std::min(static_cast<int>(_line_buf.size()), bytes_per_line);
			const auto file_offset = static_cast<uint32_t>(nCurrentLine) * bytes_per_line;

			_offset_buf.clear();
			append_hex_byte(_offset_buf, static_cast<uint8_t>(file_offset >> 24));
			append_hex_byte(_offset_buf, static_cast<uint8_t>(file_offset >> 16));
			append_hex_byte(_offset_buf, static_cast<uint8_t>(file_offset >> 8));
			append_hex_byte(_offset_buf, static_cast<uint8_t>(file_offset));

			_hex_buf.clear();
			_ascii_buf.clear();

			for (int i = 0; i < bytes_per_line; i++)
			{
				if (i == 8) _hex_buf += ' ';

				if (i < num_bytes)
				{
					const auto byte_val = static_cast<uint8_t>(_line_buf[i]);
					append_hex_byte(_hex_buf, byte_val);
					_hex_buf += ' ';
					_ascii_buf += byte_val >= 32 && byte_val < 127 ? static_cast<char>(byte_val) : '.';
				}
				else
				{
					_hex_buf += "   ";
					_ascii_buf += ' ';
				}
			}

			int x = left_margin;

			const auto draw_run = [&](const std::string_view text, const int chars, const pf::color_t color)
			{
				const pf::irect clip(x, y, x + chars * cx, y + cy);
				draw.draw_text(x, y, clip, text, styles.text_font, color, bg);
				x += chars * cx;
			};

			draw_run(_offset_buf, offset_chars, offset_color);
			draw_run(_hex_buf, hex_chars, hex_color);
			draw_run("|", 1, separator_color);
			draw_run(_ascii_buf, bytes_per_line, ascii_color);
			draw_run("|", 1, separator_color);

			nCurrentLine++;
			y += cy;
		}

		_vscroll.draw(draw, scrollbar_rect());
	}
};
