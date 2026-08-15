// tests.cpp — Unit tests for document editing, undo/redo, search, and utilities

#include "pch.h"
#include "app.h"
#include "document.h"
#include "app_state.h"
#include "view_doc.h"
#include "view_list_search.h"
#include "view_agent.h"
#include "agent_host.h"
#include "calc.h"
#include "json.h"
#include "acp.h"
#include "agent_session.h"
#include "test.h"


class null_events final : public document_events
{
public:
	void invalidate(uint32_t) override
	{
	}

	void invalidate_lines(int, int) override
	{
	}

	void lines_changed(int, int) override
	{
	}

	void line_count_changed(int, int) override
	{
	}

	void ensure_visible(const text_location&) override
	{
	}
};

static null_events null_ev;

// stub_window_frame — No-op window for testing without a real platform window
struct stub_window_frame final : pf::window_frame
{
	void set_reactor(pf::frame_reactor_ptr) override
	{
	}

	void notify_size() override
	{
	}

	pf::irect get_client_rect() const override { return {}; }

	void invalidate() override
	{
	}

	void invalidate_rect(const pf::irect&) override
	{
	}

	void set_focus() override
	{
	}

	bool has_focus() const override { return false; }

	void set_capture() override
	{
	}

	void release_capture() override
	{
	}

	uint32_t set_timer(uint32_t, uint32_t) override { return 0; }

	void kill_timer(uint32_t) override
	{
	}

	pf::ipoint screen_to_client(const pf::ipoint pt) const override { return pt; }

	void set_cursor_shape(pf::cursor_shape) override
	{
	}

	void move_window(const pf::irect&) override
	{
	}

	void show(bool) override
	{
	}

	bool is_visible() const override { return false; }

	void set_text(std::string_view) override
	{
	}

	placement get_placement() const override { return {}; }

	void set_placement(const placement&) override
	{
	}

	void track_mouse_leave() override
	{
	}

	bool is_key_down(unsigned int) const override { return false; }
	bool is_key_down_async(unsigned int) const override { return false; }

	pf::window_frame_ptr create_child(std::string_view, uint32_t, pf::color_t) const & override
	{
		return std::make_shared<stub_window_frame>();
	}

	void close() override
	{
	}

	std::string text_from_clipboard() override { return {}; }
	bool text_to_clipboard(std::string_view) override { return false; }

	void present_pixels(const uint32_t*, int, int) override
	{
	}

	pf::toolbar_frame_ptr create_address_bar(const pf::address_bar_config&) override { return nullptr; }

	int message_box(std::string_view, std::string_view, uint32_t) override { return 0; }

	void set_menu(std::vector<pf::menu_command>) override
	{
	}

	std::unique_ptr<pf::measure_context> create_measure_context() const override { return nullptr; }

	void show_popup_menu(const std::vector<pf::menu_command>&, const pf::ipoint&) override
	{
	}

	double get_dpi_scale() const override { return 1.0; }

	void accept_drop_files(bool) override
	{
	}
};

// stub_measure_context — fixed monospace metrics so layout is deterministic in tests
struct stub_measure_context final : pf::measure_context
{
	pf::isize measure_text(const std::string_view text, const pf::font&) const override
	{
		return {static_cast<int>(pf::utf8_codepoint_count(text)) * 8, 16};
	}

	pf::isize measure_char(const pf::font&) const override { return {8, 16}; }
};

static void insert_chars(const document_ptr& doc, const std::string_view chars,
                         text_location location = text_location(0, 0))
{
	undo_group ug(doc);

	for (const auto c : chars)
	{
		location = doc->insert_text(ug, location, c);
	}
}

static void test_edit_undo_redo(const char* initial, const char* expected,
                                const std::function<void(const document_ptr&, undo_group&)>& edit)
{
	const auto doc = std::make_shared<document>(null_ev, initial);
	{
		undo_group ug(doc);
		edit(doc, ug);
		should::is_equal(expected, doc->str());
	}
	doc->undo();
	should::is_equal(initial, doc->str(), "undo");
	doc->redo();
	should::is_equal(expected, doc->str(), "redo");
}

static void should_insert_single_chars()
{
	const auto text1 = "Hello\nWorld";
	const auto text2 = "Line\n";


	const auto doc = std::make_shared<document>(null_ev);

	insert_chars(doc, text1);
	should::is_equal(text1, doc->str());

	insert_chars(doc, text2);
	should::is_equal(std::string(text2) + text1, doc->str());
}

static void should_split_line()
{
	test_edit_undo_redo("line of text", "line\n of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(4, 0), u8'\n');
	});
}

static void should_combine_line()
{
	test_edit_undo_redo("line \nof text", "line of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(0, 1));
	});
}

static void should_delete_chars()
{
	test_edit_undo_redo("one\ntwo\nthree", "oe\nto\ntree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(2, 0));
		doc->delete_text(ug, text_location(2, 1));
		doc->delete_text(ug, text_location(2, 2));
	});
}

static void should_delete_selection()
{
	test_edit_undo_redo("line of text", "lixt", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 10, 0));
	});
}

static void should_delete2_line_selection()
{
	test_edit_undo_redo("one\ntwo\nthree", "onree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 2, 2));
	});
}

static void should_delete1_line_selection()
{
	test_edit_undo_redo("one\ntwo\nthree", "on", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 1, 2, 2));
		should::is_equal("one\ntwree", doc->str());
		doc->delete_text(ug, text_selection(2, 0, 5, 1));
	});
}

static void should_insert_selection()
{
	test_edit_undo_redo("line of text", "line oone\ntwo\nthreef text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(6, 0), "one\ntwo\nthree");
	});
}

static void should_insert_crlf_text()
{
	test_edit_undo_redo("ab", "aone\ntwo\nthreeb", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(1, 0), "one\r\ntwo\r\nthree");
	});
}

static void should_return_selection()
{
	const auto doc = std::make_shared<document>(null_ev, "one\ntwo\nthree");

	should::is_equal("e\ntwo\nth", combine(doc->text(text_selection(2, 0, 2, 2))));
}

static void should_cut_and_paste()
{
	test_edit_undo_redo("one\ntwo\nthree", "one\none\ntwo\nthreetwo\nthree",
	                    [](const document_ptr& doc, undo_group& ug)
	                    {
		                    doc->insert_text(ug, text_location(0, 1), doc->str());
	                    });
}

static void should_reformat_json_preserves_strings()
{
	// Structural characters ({ } : ,) that appear inside string literals must be
	// emitted verbatim and not treated as JSON structure.
	const auto doc = std::make_shared<document>(
		null_ev, "{\"url\":\"http://example.com\",\"note\":\"a, b {x}\"}");
	doc->reformat_json();
	const auto result = doc->str();

	should::is_equal_true(result.find("http://example.com") != std::string::npos,
	                      "colon/slashes inside string preserved");
	should::is_equal_true(result.find("a, b {x}") != std::string::npos,
	                      "comma and braces inside string preserved");
}

static void should_ignore_carriage_return_char()
{
	const auto doc = std::make_shared<document>(null_ev, "ab");
	{
		undo_group ug(doc);
		doc->insert_text(ug, text_location(1, 0), '\r');
	}

	should::is_equal("ab", doc->str());
	should::is_equal(false, doc->can_undo(), "CR records no undo step");
	should::is_equal(false, doc->is_modified(), "CR does not modify");
}

static void should_delete_back_multibyte_char()
{
	const std::string_view initial = "a\xC3\xA9" "b"; // a + U+00E9 + b
	const auto doc = std::make_shared<document>(null_ev, initial);
	{
		undo_group ug(doc);
		doc->delete_text(ug, text_location(3, 0));
	}

	should::is_equal("ab", doc->str(), "whole codepoint erased");
	doc->undo();
	should::is_equal(initial, doc->str(), "undo restores the codepoint");
	doc->redo();
	should::is_equal("ab", doc->str(), "redo");
}

static void should_max_line_length_grows_on_typing()
{
	const auto doc = std::make_shared<document>(null_ev, "ab\nlonger line");
	should::is_equal(11, doc->max_line_length(), "initial longest line");

	{
		undo_group ug(doc);
		auto location = text_location(2, 0);
		for (const auto c : std::string_view("cdefghijklmn"))
			location = doc->insert_text(ug, location, c);
	}

	should::is_equal(14, doc->max_line_length(), "max line length after typing");
}

static double calc_value(const std::string_view expression)
{
	calc_parser parser(expression);
	const auto value = parser.parse();
	should::is_equal_true(value.has_value(), std::format("parse '{}': {}", expression, parser.error()));
	return value.value_or(0.0);
}

static void should_calc_expressions()
{
	should::is_equal_true(calc_value("2+3*4") == 14.0, "precedence");
	should::is_equal_true(calc_value("(2+3)*4") == 20.0, "parentheses");
	should::is_equal_true(calc_value("-3+5") == 2.0, "unary minus");
	should::is_equal_true(calc_value("1.5*2") == 3.0, "decimal point");
	should::is_equal_true(calc_value("10/4") == 2.5, "division");
	should::is_equal_true(calc_value(" 1 + 2 ") == 3.0, "whitespace");
}

static void should_calc_rejects_bad_input()
{
	calc_parser div_zero("1/0");
	should::is_equal(false, div_zero.parse().has_value(), "division by zero rejected");
	should::is_equal("Division by zero.", div_zero.error());

	calc_parser trailing("1+2 abc");
	should::is_equal(false, trailing.parse().has_value(), "trailing garbage rejected");

	calc_parser unbalanced("(1+2");
	should::is_equal(false, unbalanced.parse().has_value(), "missing paren rejected");

	calc_parser empty("");
	should::is_equal(false, empty.parse().has_value(), "empty input rejected");
}

// ── util.h string tests ────────────────────────────────────────────────────────

static void should_to_lower()
{
	should::is_equal(L'a', pf::to_lower(L'A'));
	should::is_equal(L'z', pf::to_lower(L'Z'));
	should::is_equal(L'a', pf::to_lower(L'a'));
	should::is_equal(L'5', pf::to_lower(L'5'));
}

static void should_unquote()
{
	should::is_equal("hello", pf::unquote("\"hello\""));
	should::is_equal("hello", pf::unquote("'hello'"));
	should::is_equal("hello", pf::unquote("hello"));
	should::is_equal("", pf::unquote(""));
}

static void should_icmp()
{
	should::is_equal(0, pf::icmp("Hello", "hello"));
	should::is_equal(0, pf::icmp("", ""));
	should::is_equal_true(pf::icmp("abc", "def") < 0);
	should::is_equal_true(pf::icmp("def", "abc") > 0);
	should::is_equal_true(pf::icmp("ab", "abc") < 0);
	should::is_equal_true(pf::icmp("abc", "ab") > 0);
	should::is_equal(-1, pf::icmp("", "a"));
	should::is_equal(1, pf::icmp("a", ""));
}

static void should_find_in_text()
{
	should::is_equal(0, static_cast<int>(find_in_text("Hello World", "hello")));
	should::is_equal(6, static_cast<int>(find_in_text("Hello World", "world")));
	should::is_equal(static_cast<int>(std::string_view::npos),
	                 static_cast<int>(find_in_text("Hello", "xyz")));
	should::is_equal(static_cast<int>(std::string_view::npos),
	                 static_cast<int>(find_in_text("", "abc")));
	should::is_equal(static_cast<int>(std::string_view::npos),
	                 static_cast<int>(find_in_text("abc", "")));
}

static void should_combine_lines()
{
	std::vector<std::string_view> lines = {"one", "two", "three"};
	should::is_equal("one\ntwo\nthree", combine(lines));
	should::is_equal("one, two, three", combine(lines, ", "));

	std::vector<std::string_view> single = {"only"};
	should::is_equal("only", combine(single));
}

static void should_replace_string()
{
	should::is_equal("hello world", replace("hello there", "there", "world"));
	should::is_equal("aXbXc", replace("a.b.c", ".", "X"));
	should::is_equal("unchanged", replace("unchanged", "xyz", "abc"));
	should::is_equal("abc", replace("abc", "", "X")); // empty find must not loop forever
}

static void should_is_empty()
{
	should::is_equal_true(pf::is_empty(static_cast<const char*>(nullptr)));
	should::is_equal_true(pf::is_empty(""));
	should::is_equal(false, pf::is_empty("x"));
}

// ── util.h geometry tests ──────────────────────────────────────────────────────

static void should_ipoint_ops()
{
	constexpr pf::ipoint a(3, 4);
	constexpr pf::ipoint b(1, 2);
	constexpr auto sum = a + b;
	should::is_equal(4, sum.x);
	should::is_equal(6, sum.y);

	constexpr auto neg = -a;
	should::is_equal(-3, neg.x);
	should::is_equal(-4, neg.y);

	should::is_equal_true(pf::ipoint(1, 2) == pf::ipoint(1, 2));
	should::is_equal(false, pf::ipoint(1, 2) == pf::ipoint(3, 4));
}

static void should_isize_ops()
{
	should::is_equal_true(pf::isize(10, 20) == pf::isize(10, 20));
	should::is_equal(false, pf::isize(10, 20) == pf::isize(10, 21));
}

static void should_irect_ops()
{
	const pf::irect r(10, 20, 110, 120);
	should::is_equal(100, r.width());
	should::is_equal(100, r.height());

	should::is_equal_true(r.contains(pf::ipoint(50, 50)));
	should::is_equal(false, r.contains(pf::ipoint(0, 0)));
	should::is_equal(false, r.contains(pf::ipoint(200, 200)));

	const auto offset = r.offset(5, 10);
	should::is_equal(15, offset.left);
	should::is_equal(30, offset.top);

	const auto inflated = r.inflate(2);
	should::is_equal(8, inflated.left);
	should::is_equal(18, inflated.top);
	should::is_equal(112, inflated.right);
	should::is_equal(122, inflated.bottom);

	const pf::irect a(0, 0, 10, 10);
	const pf::irect b(5, 5, 15, 15);
	const pf::irect c(20, 20, 30, 30);
	should::is_equal_true(a.intersects(b));
	should::is_equal(false, a.intersects(c));
}

// ── util.h misc tests ──────────────────────────────────────────────────────────

static void should_clamp_value()
{
	should::is_equal(5, std::clamp(5, 0, 10));
	should::is_equal(0, std::clamp(-1, 0, 10));
	should::is_equal(10, std::clamp(15, 0, 10));
	should::is_equal(5, std::clamp(5, 5, 5));
}

static void should_fnv1a_hash()
{
	const auto h1 = pf::fnv1a_i("hello");
	const auto h2 = pf::fnv1a_i("HELLO");
	const auto h3 = pf::fnv1a_i("world");
	should::is_equal(static_cast<int>(h1), static_cast<int>(h2));
	should::is_equal_true(h1 != h3);
}

// ── pf::file_path tests ────────────────────────────────────────────────────────────

static void should_file_path_ops()
{
	should::is_equal(6u, pf::file_path::find_ext("readme.txt"));
	should::is_equal(4u, pf::file_path::find_ext("test.cpp"));
	should::is_equal(4u, pf::file_path::find_ext("none"));

	should::is_equal(4u, pf::file_path::find_last_slash("src/file.cpp"));
	should::is_equal(4u, pf::file_path::find_last_slash("src\\file.cpp"));

	const pf::file_path p("C:\\code\\project");
	const auto combined = p.combine("file.txt");
	should::is_equal("C:\\code\\project\\file.txt", combined.view());

	should::is_equal_true(pf::file_path::is_path_sep(L'/'));
	should::is_equal_true(pf::file_path::is_path_sep(L'\\'));
	should::is_equal(false, pf::file_path::is_path_sep(L'x'));

	// Path separator normalization
	should::is_equal_true(pf::file_path{"c:\\folder"} == pf::file_path{"c:/folder"}, "slash vs backslash");
	should::is_equal("c:\\folder", pf::file_path{"c:/folder"}.view(), "forward slash normalized");
	should::is_equal("c:\\folder", pf::file_path{"c:\\folder\\"}.view(), "trailing sep stripped");
	should::is_equal("C:\\", pf::file_path{"C:\\"}.view(), "root preserved");
}

// ── encoding detection tests ───────────────────────────────────────────────────

static void check_encoding(const uint8_t* data, const size_t size, file_encoding expected_enc,
                           const int expected_header, const std::string_view msg)
{
	int headerLen = 0;
	const auto enc = detect_encoding(data, size, headerLen);
	should::is_equal(static_cast<int>(expected_enc), static_cast<int>(enc), msg);
	should::is_equal(expected_header, headerLen, std::string(msg) + " header");
}

static void should_detect_utf8_bom()
{
	constexpr uint8_t data[] = {0xEF, 0xBB, 0xBF, 'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 3, "UTF-8 BOM");
}

static void should_detect_utf16le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 2, "UTF-16 LE BOM");
}

static void should_detect_utf16be_bom()
{
	constexpr uint8_t data[] = {0xFE, 0xFF, 0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 2, "UTF-16 BE BOM");
}

static void should_detect_utf32le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 'H', 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, "UTF-32 LE BOM");
}

static void should_detect_utf32be_bom()
{
	constexpr uint8_t data[] = {0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 'H'};
	check_encoding(data, sizeof(data), file_encoding::utf32be, 4, "UTF-32 BE BOM");
}

static void should_detect_utf16le_no_bom()
{
	constexpr uint8_t data[] = {'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 0, "UTF-16 LE no BOM");
}

static void should_detect_utf16be_no_bom()
{
	constexpr uint8_t data[] = {0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 0, "UTF-16 BE no BOM");
}

static void should_detect_utf8_default()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, "default UTF-8");
}

static void should_detect_utf8_without_bom()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, "UTF-8 without BOM");
}

static void should_detect_small_files()
{
	constexpr uint8_t one[] = {'A'};
	check_encoding(one, 1, file_encoding::utf8, 0, "1-byte file");

	constexpr uint8_t bom16[] = {0xFF, 0xFE};
	check_encoding(bom16, 2, file_encoding::utf16, 2, "2-byte UTF-16 LE BOM");

	constexpr uint8_t bom8[] = {0xEF, 0xBB, 0xBF};
	check_encoding(bom8, 3, file_encoding::utf8, 3, "3-byte UTF-8 BOM");
}

static void should_prioritize_utf32_over_utf16_bom()
{
	const uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, "UTF-32 LE over UTF-16 LE");
}

// ── encoding conversion tests ──────────────────────────────────────────────────

static void should_utf8_to_utf16_ascii()
{
	// Pure ASCII round-trips through UTF-8 conversion
	should::is_equal_true(pf::utf8_to_utf16("Hello") == L"Hello", "ascii utf8 to utf16");
	should::is_equal_true(pf::utf8_to_utf16("").empty(), "empty utf8 to utf16");
}

static void should_utf8_to_utf16_multibyte()
{
	// Em-dash U+2014 is encoded as E2 80 94 in UTF-8
	const std::string utf8_emdash = "\xE2\x80\x94";
	const auto result = pf::utf8_to_utf16(utf8_emdash);
	should::is_equal(1, static_cast<int>(result.size()), "em-dash length");
	should::is_equal(0x2014, result[0], "em-dash codepoint");
}

static void should_utf16_to_utf8_roundtrip()
{
	// Em-dash U+2014 round-trip through UTF-16 -> UTF-8 -> UTF-16
	const std::wstring emdash(1, 0x2014);
	const auto utf8 = pf::utf16_to_utf8(emdash);
	should::is_equal(3, static_cast<int>(utf8.size()), "em-dash UTF-8 byte count");
	should::is_equal(0xE2, static_cast<uint8_t>(utf8[0]), "em-dash byte 0");
	should::is_equal(0x80, static_cast<uint8_t>(utf8[1]), "em-dash byte 1");
	should::is_equal(0x94, static_cast<uint8_t>(utf8[2]), "em-dash byte 2");

	const auto back = pf::utf8_to_utf16(utf8);
	should::is_equal_true(emdash == back, "em-dash round-trip");
}

static void should_utf8_to_utf16_mixed()
{
	// Mixed ASCII and multi-byte: "key — value"
	// U+2014 em-dash = E2 80 94 in UTF-8
	const std::string utf8 = "key \xE2\x80\x94 value";
	const auto result = pf::utf8_to_utf16(utf8);
	// Should be: 'k','e','y',' ', U+2014, ' ','v','a','l','u','e'
	should::is_equal(11, static_cast<int>(result.size()), "mixed UTF-8 length");
	should::is_equal(L'k', result[0], "mixed char 0");
	should::is_equal(0x2014, result[4], "mixed em-dash");
	should::is_equal(L'v', result[6], "mixed char after em-dash");
}

static void should_utf8_to_utf16_various_symbols()
{
	// Euro sign U+20AC = E2 82 AC
	should::is_equal(0x20AC,
	                 pf::utf8_to_utf16("\xE2\x82\xAC")[0],
	                 "euro sign");

	// Copyright U+00A9 = C2 A9
	should::is_equal(0x00A9,
	                 pf::utf8_to_utf16("\xC2\xA9")[0],
	                 "copyright sign");

	// Japanese Hiragana 'A' U+3042 = E3 81 82
	{
		const std::string hiragana_utf8 = "\xE3\x81\x82";
		should::is_equal(0x3042,
		                 pf::utf8_to_utf16(hiragana_utf8)[0],
		                 "hiragana A");
	}
}

// ── line ending detection tests ────────────────────────────────────────────────

static void check_line_ending(const uint8_t* data, const size_t size, line_endings expected,
                              const std::string_view msg)
{
	const auto le = detect_line_endings(data, static_cast<int>(size));
	should::is_equal(static_cast<int>(expected), static_cast<int>(le), msg);
}

static void should_detect_crlf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0D, 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_dos, "CRLF detection");
}

static void should_detect_lf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, "LF detection");
}

static void should_detect_lfcr_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 0x0D, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, "LFCR detection");
}

static int test_md_parse(const char* text, text_block* blocks)
{
	int count = 0;
	const document_line line(std::string_view{text});
	const auto highlighter = select_highlighter(doc_type::markdown, {});
	std::string line_view;
	line.render(line_view);
	highlighter(0, line_view, blocks, count);
	return count;
}

static void should_md_highlight_heading()
{
	text_block blocks[64];

	const auto h1_count = test_md_parse("# Hello", blocks);
	should::is_equal(2, h1_count, "md h1 block count");
	should::is_equal(0, blocks[0]._char_pos, "md h1 marker pos");
	should::is_equal(2, blocks[1]._char_pos, "md h1 content pos");

	const auto h2_count = test_md_parse("## World", blocks);
	should::is_equal(2, h2_count, "md h2 block count");
	should::is_equal(0, blocks[0]._char_pos, "md h2 marker pos");
	should::is_equal(3, blocks[1]._char_pos, "md h2 content pos");
}

static void should_md_highlight_bold()
{
	text_block blocks[64];
	const auto count = test_md_parse("some **bold** text", blocks);
	should::is_equal_true(count >= 3, "md bold block count");
}

static void should_md_highlight_italic()
{
	text_block blocks[64];
	const auto count = test_md_parse("some *italic* text", blocks);
	should::is_equal_true(count >= 3, "md italic block count");
}

static void should_md_highlight_link()
{
	text_block blocks[64];
	const auto count = test_md_parse("click [here](http://example.com) now", blocks);
	should::is_equal_true(count >= 5, "md link block count");
}

static void should_md_highlight_list()
{
	text_block blocks[64];
	const auto count = test_md_parse("- list item", blocks);
	should::is_equal_true(count >= 2, "md list block count");
	should::is_equal(0, blocks[0]._char_pos, "md list bullet pos");
}

// ── Code highlighting ──────────────────────────────────────────────────────────────────

struct highlight_result
{
	text_block blocks[256];
	int count = 0;
	uint32_t cookie = 0;

	[[nodiscard]] style style_at(const int char_pos) const
	{
		auto result = style::normal_text;
		for (int i = 0; i < count && blocks[i]._char_pos <= char_pos; i++)
			result = blocks[i]._color;
		return result;
	}
};

static highlight_result run_highlighter(const pf::file_path& path, const std::string_view text,
                                        const uint32_t cookie_in = 0)
{
	highlight_result result;
	const auto highlighter = select_highlighter(doc_type::text, path);
	result.cookie = highlighter(cookie_in, text, result.blocks, result.count);
	return result;
}

static void should_highlight_cpp()
{
	const pf::file_path path{"a.cpp"};

	const auto keyword = run_highlighter(path, "int x = 42;");
	should::is_equal(static_cast<int>(style::code_keyword), static_cast<int>(keyword.style_at(0)),
	                 "cpp keyword");
	should::is_equal(static_cast<int>(style::code_number), static_cast<int>(keyword.style_at(8)),
	                 "cpp number");

	const auto comment = run_highlighter(path, "x = 1; // trailing");
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(comment.style_at(8)),
	                 "cpp line comment");

	const auto str = run_highlighter(path, "auto s = \"text\";");
	should::is_equal(static_cast<int>(style::code_string), static_cast<int>(str.style_at(10)),
	                 "cpp string");

	const auto prep = run_highlighter(path, "#include <vector>");
	should::is_equal(static_cast<int>(style::code_preprocessor), static_cast<int>(prep.style_at(1)),
	                 "cpp preprocessor");

	// A block comment carries into the next line through the cookie
	const auto opened = run_highlighter(path, "/* start");
	should::is_equal_true(opened.cookie != 0, "cpp block comment opens");
	const auto carried = run_highlighter(path, "still comment", opened.cookie);
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(carried.style_at(0)),
	                 "cpp block comment carries");
	const auto closed = run_highlighter(path, "end */", opened.cookie);
	should::is_equal(0, static_cast<int>(closed.cookie), "cpp block comment closes");

	// An escaped quote does not end the string
	const auto escaped = run_highlighter(path, "\"a\\\"b\" + c");
	should::is_equal(static_cast<int>(style::code_string), static_cast<int>(escaped.style_at(4)),
	                 "cpp escaped quote stays in string");
}

static void should_highlight_rust()
{
	const pf::file_path path{"a.rs"};

	const auto keyword = run_highlighter(path, "fn main() {}");
	should::is_equal(static_cast<int>(style::code_keyword), static_cast<int>(keyword.style_at(0)),
	                 "rust keyword");

	const auto comment = run_highlighter(path, "let x = 1; // note");
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(comment.style_at(12)),
	                 "rust line comment");

	// Rust has no preprocessor: an attribute is not a directive
	const auto attribute = run_highlighter(path, "#[derive(Debug)]");
	should::is_equal(0, static_cast<int>(attribute.cookie), "rust attribute is not preprocessor");
	should::is_equal(false, attribute.style_at(1) == style::code_preprocessor,
	                 "rust attribute not coloured as preprocessor");

	const auto opened = run_highlighter(path, "/* start");
	should::is_equal_true(opened.cookie != 0, "rust block comment opens");
}

static void should_highlight_python()
{
	const pf::file_path path{"a.py"};

	const auto keyword = run_highlighter(path, "def main():");
	should::is_equal(static_cast<int>(style::code_keyword), static_cast<int>(keyword.style_at(0)),
	                 "python keyword");

	const auto comment = run_highlighter(path, "x = 1  # note");
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(comment.style_at(7)),
	                 "python comment");

	// '#' is a comment, not a C-style preprocessor directive
	const auto hash_first = run_highlighter(path, "# whole line");
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(hash_first.style_at(0)),
	                 "python leading hash is a comment");

	// A slash pair is division, not a comment
	const auto div = run_highlighter(path, "y = a // b");
	should::is_equal(false, div.style_at(7) == style::code_comment,
	                 "python floor division is not a comment");

	const auto str = run_highlighter(path, "s = 'text'");
	should::is_equal(static_cast<int>(style::code_string), static_cast<int>(str.style_at(5)),
	                 "python string");
}

static void should_highlight_powershell()
{
	const pf::file_path path{"a.ps1"};

	const auto keyword = run_highlighter(path, "function Get-Thing {}");
	should::is_equal(static_cast<int>(style::code_keyword), static_cast<int>(keyword.style_at(0)),
	                 "ps1 keyword");

	// PowerShell keywords are case-insensitive
	const auto upper = run_highlighter(path, "FOREACH ($x in $y) {}");
	should::is_equal(static_cast<int>(style::code_keyword), static_cast<int>(upper.style_at(0)),
	                 "ps1 keyword is case insensitive");

	const auto comment = run_highlighter(path, "$x = 1 # note");
	should::is_equal(static_cast<int>(style::code_comment), static_cast<int>(comment.style_at(7)),
	                 "ps1 comment");

	// Backtick is the escape character, so a backslash does not escape a quote
	const auto backslash = run_highlighter(path, "\"a\\\" + $b");
	should::is_equal(false, backslash.style_at(8) == style::code_string,
	                 "ps1 backslash does not escape a quote");

	const auto opened = run_highlighter(path, "<# start");
	should::is_equal_true(opened.cookie != 0, "ps1 block comment opens");
	const auto closed = run_highlighter(path, "end #>", opened.cookie);
	should::is_equal(0, static_cast<int>(closed.cookie), "ps1 block comment closes");
}

static void should_highlight_plain_text()
{
	const auto result = run_highlighter(pf::file_path{"a.txt"}, "value 42 here");
	should::is_equal(static_cast<int>(style::code_number), static_cast<int>(result.style_at(6)),
	                 "plain text number");
	should::is_equal(0, static_cast<int>(result.cookie), "plain text has no cookie");
}

// A word scan must never stop between the bytes of one codepoint, or the spell
// checker sees fragments and flags correctly spelled words
static void should_treat_utf8_bytes_as_word_bytes()
{
	constexpr std::string_view cafe = "caf\xC3\xA9 au lait";

	int end = 0;
	while (end < static_cast<int>(cafe.size()) && is_spell_word_byte(cafe[end]))
		end++;

	should::is_equal(5, end, "accented word scanned whole");
	should::is_equal(false, is_spell_word_byte(' '), "space is not a word byte");
	should::is_equal_true(is_spell_word_byte('a'), "letter is a word byte");
}

static void should_find_text_ignoring_case_for_non_ascii()
{
	// A hex escape swallows following hex digits, so the literals are split at each boundary
	constexpr std::string_view ecole_upper = "\xC3\x89" "COLE";
	constexpr std::string_view ecole_lower = "\xC3\xA9" "cole";

	should::is_equal(static_cast<size_t>(0), find_in_text(ecole_upper, ecole_lower),
	                 "accented match folds case");
	should::is_equal(static_cast<size_t>(4), find_in_text("the caf\xC3\x89 here", "caf\xC3\xA9"),
	                 "accented match at offset");
	should::is_equal(std::string_view::npos, find_in_text("caf\xC3\xA9", "caf\xC3\xA8"),
	                 "different accent does not match");
	should::is_equal(std::string_view::npos, find_in_text(ecole_upper, ecole_lower, true),
	                 "case sensitive rejects the fold");

	// A multi-byte character must not be matched from inside its own bytes
	should::is_equal(static_cast<size_t>(0), find_in_text("\xC3\xA9\xC3\xA9", "\xC3\xA9"),
	                 "match starts on a codepoint boundary");

	should::is_equal(static_cast<size_t>(6), find_in_text("plain ASCII text", "ascii"),
	                 "ascii path still folds case");
}

static void should_apply_gitignore_rules()
{
	gitignore_rules rules;
	rules.add_file("# comment\n\nbin/\n*.obj\n/root-only.txt\ndocs/*.tmp\n!keep.obj\n", {});

	should::is_equal_true(rules.is_ignored("bin", true), "directory rule ignores the folder");
	should::is_equal(false, rules.is_ignored("bin", false), "directory rule spares a file");
	should::is_equal_true(rules.is_ignored("src/deep/bin", true), "directory rule applies at any depth");

	should::is_equal_true(rules.is_ignored("a.obj", false), "extension rule");
	should::is_equal_true(rules.is_ignored("src/a.obj", false), "extension rule at depth");
	should::is_equal(false, rules.is_ignored("keep.obj", false), "negation re-includes");

	should::is_equal_true(rules.is_ignored("root-only.txt", false), "anchored rule at the root");
	should::is_equal(false, rules.is_ignored("src/root-only.txt", false), "anchored rule not at depth");

	should::is_equal_true(rules.is_ignored("docs/notes.tmp", false), "path rule matches");
	should::is_equal(false, rules.is_ignored("docs/sub/notes.tmp", false), "'*' does not cross a slash");

	should::is_equal(false, rules.is_ignored("src/main.cpp", false), "unmatched file is kept");
}

static void should_apply_nested_gitignore_relative_to_its_folder()
{
	gitignore_rules rules;
	rules.add_file("/local.txt\n", "src");

	should::is_equal_true(rules.is_ignored("src/local.txt", false), "anchored to its own folder");
	should::is_equal(false, rules.is_ignored("local.txt", false), "does not apply above its folder");
	should::is_equal(false, rules.is_ignored("other/local.txt", false), "does not apply to a sibling");
}

static void should_match_gitignore_double_star()
{
	gitignore_rules rules;
	rules.add_file("**/build\nlogs/**\n", {});

	should::is_equal_true(rules.is_ignored("build", true), "'**/' matches at the root");
	should::is_equal_true(rules.is_ignored("a/b/build", true), "'**/' matches at depth");
	should::is_equal_true(rules.is_ignored("logs/a/b.txt", false), "trailing '**' matches everything below");
}

// ── app_state tests ────────────────────────────────────────────────────────────────────

// sync_scheduler — Test stub that executes tasks immediately on the calling thread.
class sync_scheduler final : public async_scheduler
{
public:
	void run_async(const std::function<void()> task) override { if (task) task(); }
	void run_ui(const std::function<void()> task) override { if (task) task(); }
};

// deferred_scheduler — Queues work the way the real message loop does, so tests
// see the same ordering as production instead of everything completing inline.
class deferred_scheduler final : public async_scheduler
{
	std::vector<std::function<void()>> _async;
	std::vector<std::function<void()>> _ui;

public:
	void run_async(std::function<void()> task) override { if (task) _async.push_back(std::move(task)); }
	void run_ui(std::function<void()> task) override { if (task) _ui.push_back(std::move(task)); }

	void pump()
	{
		for (int round = 0; round < 32 && !(_async.empty() && _ui.empty()); round++)
		{
			auto async_tasks = std::exchange(_async, {});
			for (auto& t : async_tasks) t();

			auto ui_tasks = std::exchange(_ui, {});
			for (auto& t : ui_tasks) t();
		}
	}
};


static std::shared_ptr<app_state> create_test_app()
{
	auto state = std::make_shared<app_state>(std::make_shared<sync_scheduler>());
	state->on_create(std::make_shared<stub_window_frame>());
	return state;
}

static pf::file_path create_temp_test_root()
{
	const auto temp_path = pf::file_path{pf::platform_temp_file_path("rtf")};
	pf::platform_recycle_file(temp_path);
	pf::platform_create_directory(temp_path);
	return temp_path;
}

static void write_test_text_file(const pf::file_path& path, const std::string_view text)
{
	const auto handle = pf::open_file_for_write(path);
	should::is_equal_true(handle != nullptr, "test file opened for write");
	const auto written = handle->write(reinterpret_cast<const uint8_t*>(text.data()),
	                                   static_cast<uint32_t>(text.size()));
	should::is_equal_true(written == text.size(), "test file written");
}

static std::string read_test_text_file(const pf::file_path& path)
{
	const auto handle = pf::open_for_read(path);
	should::is_equal_true(handle != nullptr, "test file opened for read");
	if (!handle) return {};
	std::vector<uint8_t> data(handle->size());
	uint32_t total = 0;
	while (total < data.size())
	{
		uint32_t read = 0;
		if (!handle->read(data.data() + total, static_cast<uint32_t>(data.size() - total), &read) || read == 0)
			break;
		total += read;
	}
	return std::string(reinterpret_cast<const char*>(data.data()), total);
}

static index_item_ptr find_test_item(const index_item_ptr& item, const pf::file_path& path)
{
	if (!item)
		return nullptr;
	if (item->path == path)
		return item;

	for (const auto& child : item->children)
	{
		if (auto found = find_test_item(child, path))
			return found;
	}

	return nullptr;
}

static void should_app_state_new_doc_is_markdown()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine("new", "md"), "");

	should::is_equal_true(result.created, "new doc created");
	should::is_equal_true(is_markdown_path(state->active_item()->path), "new doc is markdown");
	should::is_equal_true(state->active_item()->path.is_save_path(), "new doc has save path");
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 "new empty doc mode is edit");
}

static void should_app_state_is_markdown_path()
{
	should::is_equal_true(is_markdown_path(pf::file_path{"readme.md"}));
	should::is_equal_true(is_markdown_path(pf::file_path{"DOC.MARKDOWN"}));
	should::is_equal(false, is_markdown_path(pf::file_path{"code.cpp"}));
	should::is_equal(false, is_markdown_path(pf::file_path{"noext"}));
}

static void should_cap_search_results()
{
	// First file alone produces more matches than the cap; the second file must
	// not push the total above max_search_results.
	std::string many_lines;
	for (int i = 0; i < max_search_results + 50; i++)
		many_lines += "x\n";

	std::vector<app_state::search_input> inputs;
	inputs.push_back({pf::file_path{"c:\\folder\\a.txt"}, std::make_shared<document>(null_ev, many_lines)});
	inputs.push_back({pf::file_path{"c:\\folder\\b.txt"}, std::make_shared<document>(null_ev, "x\nx\nx")});

	const auto results = app_state::perform_search(inputs, "x");

	int total = 0;
	for (const auto& entry : results)
		total += static_cast<int>(entry.second.size());

	should::is_equal(max_search_results, total, "search results capped");
}

// The header shows the number of matches, which must not change when a group is collapsed
static void should_count_search_results_when_group_collapsed()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	const auto file = std::make_shared<index_item>(pf::file_path{"c:\\folder\\a.txt"}, "a.txt", false);
	file->search_results = {
		{"one", 0, 0, 0, 3},
		{"two", 1, 0, 0, 3},
		{"three", 2, 0, 0, 3},
	};
	root->children.push_back(file);
	state->set_root(root);

	auto& view = *state->_search_view;
	view.populate();
	should::is_equal(3, view.result_count(), "matches counted while expanded");

	view._key_to_item[view.make_key(file)]->expanded = false;
	view.populate();
	should::is_equal(3, view.result_count(), "matches still counted while collapsed");
}

// A longer query is answered from the previous hits, so files that already failed are not reread
static void should_narrow_search_to_previous_matches()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);

	const auto hit = std::make_shared<index_item>(pf::file_path{"c:\\folder\\hit.txt"}, "hit.txt", false,
	                                              std::make_shared<document>(null_ev, "alpha beta", true));
	const auto miss = std::make_shared<index_item>(pf::file_path{"c:\\folder\\miss.txt"}, "miss.txt", false,
	                                               std::make_shared<document>(null_ev, "nothing here", true));
	root->children.push_back(hit);
	root->children.push_back(miss);
	state->set_root(root);

	state->execute_search("alp");
	should::is_equal(1, static_cast<int>(hit->search_results.size()), "prefix matched the one file");
	should::is_equal(0, static_cast<int>(miss->search_results.size()), "prefix missed the other");

	state->execute_search("alpha");
	should::is_equal(1, static_cast<int>(hit->search_results.size()), "narrowed search still matches");
	should::is_equal(0, static_cast<int>(miss->search_results.size()), "narrowed search clears the rest");

	// Backspacing widens the query again, so the full folder must be rescanned
	state->execute_search("nothing");
	should::is_equal(0, static_cast<int>(hit->search_results.size()), "widened search rescans");
	should::is_equal(1, static_cast<int>(miss->search_results.size()), "widened search finds the other file");
}

// Writing a truncated document back would replace the file with only the part that was read
static void should_refuse_to_save_truncated_document()
{
	const auto root = create_temp_test_root();
	const auto path = root.combine("big.txt");
	write_test_text_file(path, "original contents");

	const auto d = std::make_shared<document>(null_ev, "partial");
	loaded_file_data data;
	data.lines.emplace_back(std::string_view{"partial"});
	data.truncated = true;
	data.disk_modified_time = 1; // apply_loaded_data ignores data that never came from disk
	d->apply_loaded_data(path, std::move(data));

	should::is_equal_true(d->is_truncated(), "document reports truncation");
	should::is_equal(false, d->save_to_file(path), "save refused");
	should::is_equal("original contents", read_test_text_file(path), "file left untouched");
}

static void should_evict_unused_documents()
{
	const auto state = create_test_app();
	const auto disk_root = create_temp_test_root();
	const auto root = std::make_shared<index_item>(disk_root, disk_root.name(), true);
	state->set_root(root);

	constexpr int file_count = 30;

	for (int i = 0; i < file_count; i++)
	{
		const auto path = disk_root.combine(std::format("f{}.txt", i));
		write_test_text_file(path, "contents");

		auto item = std::make_shared<index_item>(path, std::string(path.name()), false,
		                                         std::make_shared<document>(null_ev, "contents"));
		root->children.push_back(item);
		state->set_active_item(item);
	}

	// The active document is kept on top of the budget
	should::is_equal(static_cast<int>(app_state::max_resident_documents) + 1,
	                 static_cast<int>(state->resident_document_count()),
	                 "resident documents capped");

	should::is_equal_true(root->children.back()->doc != nullptr, "active document kept");
	should::is_equal_true(root->children.front()->doc == nullptr, "least recently used dropped");

	// Unsaved work is never dropped, however old it is
	const auto modified = std::make_shared<index_item>(disk_root.combine("dirty.txt"), "dirty.txt", false,
	                                                   std::make_shared<document>(null_ev, "edited", true));
	write_test_text_file(modified->path, "contents");
	root->children.push_back(modified);

	for (int i = 0; i < file_count; i++)
		state->set_active_item(root->children[i]);

	should::is_equal_true(modified->doc != nullptr, "modified document pinned");
}

// Dragging text deletes the selection before reinserting it, which shifts any target below
static void should_adjust_drop_target_for_removed_selection()
{
	const text_selection same_line(5, 0, 10, 0);

	should::is_equal(2, doc_view::adjust_for_removal(same_line, text_location(2, 0)).x,
	                 "target before the selection is unchanged");
	should::is_equal(6, doc_view::adjust_for_removal(same_line, text_location(11, 0)).x,
	                 "target after the selection shifts back by its width");
	should::is_equal(0, doc_view::adjust_for_removal(same_line, text_location(11, 0)).y,
	                 "same line stays on the same line");

	const text_selection multi_line(2, 1, 4, 3);

	const auto later_line = doc_view::adjust_for_removal(multi_line, text_location(7, 5));
	should::is_equal(7, later_line.x, "a target on a later line keeps its column");
	should::is_equal(3, later_line.y, "a target on a later line moves up by the line count");

	const auto last_line = doc_view::adjust_for_removal(multi_line, text_location(9, 3));
	should::is_equal(7, last_line.x, "a target on the final selected line is rebased");
	should::is_equal(1, last_line.y, "a target on the final selected line joins the first");

	const auto earlier = doc_view::adjust_for_removal(multi_line, text_location(1, 0));
	should::is_equal(1, earlier.x, "a target above the selection is unchanged");
	should::is_equal(0, earlier.y, "a target above the selection keeps its line");
}

static void should_create_new_file_with_content()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine("notes", "md"), "# Hello");

	should::is_equal_true(result.created, "new file created");
	should::is_equal("# Hello", state->doc()->str(), "new file content");
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 "non-empty content mode is edit");
	should::is_equal(1, static_cast<int>(root->children.size()), "file added to root");
}

static void should_create_new_file_added_to_tree()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	const auto existing = std::make_shared<index_item>(pf::file_path{"c:\\folder\\old.txt"}, "old.txt", false,
	                                                   std::make_shared<document>(null_ev, "old"));
	root->children.push_back(existing);
	state->set_root(root);
	state->set_active_item(existing);

	const auto result = state->create_new_file(state->save_folder().combine("new", "md"), "");

	// New file should be added alongside existing
	should::is_equal_true(result.created, "new file created");
	should::is_equal(2, static_cast<int>(root->children.size()), "two files in root");
	// Active item should be the new file
	should::is_equal_true(state->active_item()->path != existing->path, "active switched to new");
	should::is_equal_true(is_markdown_path(state->active_item()->path), "new file is markdown");
}

static void should_create_new_file_with_unique_name()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{"c:\\folder\\new.md"}, "new.md", false, std::make_shared<document>(null_ev, "old")));
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine("new", "md"), "");

	should::is_equal_true(result.created, "unique new file created");
	should::is_equal("new-2.md", result.name, "collision resolved with numbered name");
	should::is_equal("new-2.md", state->active_item()->name, "active item uses resolved name");
	should::is_equal(2, static_cast<int>(root->children.size()), "unique file added to tree");
}

static void should_restore_per_document_view_mode()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);

	const auto markdown_path = pf::file_path{"c:\\folder\\notes.md"};
	const auto text_path = pf::file_path{"c:\\folder\\other.txt"};

	const auto markdown_doc = std::make_shared<document>(null_ev, "# Notes");
	markdown_doc->path(markdown_path);
	const auto text_doc = std::make_shared<document>(null_ev, "plain text");
	text_doc->path(text_path);

	const auto markdown_item = std::make_shared<index_item>(markdown_path, "notes.md", false, markdown_doc);
	const auto text_item = std::make_shared<index_item>(text_path, "other.txt", false, text_doc);
	root->children.push_back(markdown_item);
	root->children.push_back(text_item);
	state->set_root(root);

	state->set_active_item(markdown_item);
	should::is_equal(static_cast<int>(view_mode::markdown_files), static_cast<int>(state->get_mode()),
	                 "markdown file opens in preview");

	state->on_escape();
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 "escape switches markdown file to edit");

	state->set_active_item(text_item);
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 "text file stays in edit");

	state->set_active_item(markdown_item);
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 "markdown file restores its last view mode");
}

static void should_create_multiple_new_files()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	state->set_root(root);

	const auto first = state->create_new_file(state->save_folder().combine("a", "md"), "aaa");
	const auto second = state->create_new_file(state->save_folder().combine("b", "md"), "bbb");

	should::is_equal_true(first.created, "first file created");
	should::is_equal_true(second.created, "second file created");
	should::is_equal(2, static_cast<int>(root->children.size()), "two files created");
	should::is_equal("bbb", state->doc()->str(), "active is second file");
}

static void should_create_new_file_sorted_in_tree()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{"c:\\folder\\zeta.md"}, "zeta.md", false, std::make_shared<document>(null_ev, "z")));
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{"c:\\folder\\beta.md"}, "beta.md", false, std::make_shared<document>(null_ev, "b")));
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine("alpha", "md"), "");

	should::is_equal_true(result.created, "sorted file created");
	should::is_equal("alpha.md", root->children[0]->name, "new file inserted in sorted position");
	should::is_equal("beta.md", root->children[1]->name, "existing file order preserved");
	should::is_equal("zeta.md", root->children[2]->name, "later file remains last");
}

static void should_create_new_folder_with_unique_name()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	const auto existing_path = root_path.combine("new-folder");
	should::is_equal_true(pf::platform_create_directory(existing_path), "existing folder created");
	state->set_root(app_state::load_index(root_path, {}));

	const auto result = state->create_new_folder(root_path);

	should::is_equal_true(result.created, "new folder created");
	should::is_equal("new-folder-2", result.name, "folder collision resolved with numbered name");
	should::is_equal_true(pf::is_directory(result.path), "resolved folder exists on disk");

	pf::platform_recycle_file(root_path);
}

static void should_refresh_index_preserve_unsaved_doc_folder()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	const auto notes_path = root_path.combine("notes");
	should::is_equal_true(pf::platform_create_directory(notes_path), "notes folder created");

	state->set_root(app_state::load_index(root_path, {}));
	const auto created = state->create_new_file(notes_path.combine("draft", "md"), "draft");
	should::is_equal_true(created.created, "draft file created in nested folder");

	state->refresh_index(root_path);

	const auto refreshed_notes = find_test_item(state->root_item(), notes_path);
	const auto refreshed_draft = find_test_item(state->root_item(), created.path);

	should::is_equal_true(refreshed_notes != nullptr, "notes folder still exists after refresh");
	should::is_equal_true(refreshed_draft != nullptr, "draft file preserved after refresh");
	should::is_equal_true(refreshed_draft != state->root_item(), "draft file not promoted to root");
	should::is_equal_true(find_test_item(refreshed_notes, created.path) != nullptr,
	                      "draft file remains under original folder");

	pf::platform_recycle_file(root_path);
}

static void should_remember_recent_root_folders_most_recent_first()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	state->remember_root_folder(pf::file_path{"c:\\one"});
	state->remember_root_folder(pf::file_path{"c:\\two"});
	state->remember_root_folder(pf::file_path{"c:\\one"});

	should::is_equal(2, static_cast<int>(state->recent_root_folders().size()), "deduped list size");
	should::is_equal("c:\\one", state->recent_root_folders()[0].view(), "latest folder moved to front");
	should::is_equal("c:\\two", state->recent_root_folders()[1].view(), "older folder shifted back");
}

static void should_cap_recent_root_folders_at_eight()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	for (int i = 0; i < 10; ++i)
		state->remember_root_folder(pf::file_path{std::format("c:\\folder{}", i)});

	should::is_equal(8, static_cast<int>(state->recent_root_folders().size()), "capped at eight");
	should::is_equal("c:\\folder9", state->recent_root_folders()[0].view(), "newest folder kept first");
	should::is_equal("c:\\folder2", state->recent_root_folders()[7].view(), "oldest retained folder kept last");
}

static void should_select_word_containing_non_ascii()
{
	// "café latte" — é is two bytes, so a byte-wise scan would stop inside it
	const auto d = std::make_shared<document>(null_ev, "caf\xC3\xA9 latte");

	const auto sel = d->word_selection(text_location(1, 0), false);
	should::is_equal(0, sel._start.x, "word starts at line start");
	should::is_equal(5, sel._end.x, "word ends after the two-byte codepoint");

	d->move_to(text_location(0, 0), false);
	d->move_word_right(false);
	should::is_equal(6, d->cursor_pos().x, "word right lands past the trailing space");

	d->move_word_left(false);
	should::is_equal(0, d->cursor_pos().x, "word left returns to the start of the word");
}

static void should_insert_non_ascii_codepoint_as_utf8()
{
	const auto d = std::make_shared<document>(null_ev, "");

	{
		undo_group ug(d);
		d->insert_text(ug, text_location(0, 0), pf::utf8_encode(U'\u00e9'));
	}

	should::is_equal("\xC3\xA9", d->str(), "codepoint stored as UTF-8");

	{
		undo_group ug(d);
		d->insert_text(ug, text_location(2, 0), pf::utf8_encode(U'\U0001F600'));
	}

	should::is_equal("\xC3\xA9\xF0\x9F\x98\x80", d->str(), "supplementary codepoint stored as UTF-8");

	d->edit_undo();
	should::is_equal("\xC3\xA9", d->str(), "undo removes the whole codepoint");
}

static void should_keep_max_line_length_after_editing_a_short_line()
{
	const std::string long_line(50, 'x');
	const auto d = std::make_shared<document>(null_ev, "ab\n" + long_line);

	{
		undo_group ug(d);
		d->insert_text(ug, text_location(2, 0), "c"); // edit the short first line
	}

	should::is_equal(50, d->max_line_length(), "longest line still drives the max");
}

static void should_block_edits_to_read_only_document()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{"c:\\folder"}, "folder", true);
	state->set_root(root);
	state->create_new_file(state->save_folder().combine("data", "txt"), "b\na\nb\n");

	should::is_equal_true(state->can_edit_document(), "writable document allows editing commands");

	state->doc()->read_only(true);
	should::is_equal_true(!state->can_edit_document(), "read-only document blocks editing commands");

	const auto before = state->doc()->str();
	state->doc()->sort_remove_duplicates();
	should::is_equal(before, state->doc()->str(), "sort leaves a read-only document unchanged");
}

static void should_restore_recent_root_folders_in_saved_order()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	const app_state::recent_root_entry entries[] = {
		{pf::file_path{"c:\\newest"}, pf::file_path{"c:\\newest\\a.md"}},
		{pf::file_path{"c:\\middle"}, pf::file_path{"c:\\middle\\b.md"}},
		{pf::file_path{"c:\\oldest"}, pf::file_path{"c:\\oldest\\c.md"}},
	};

	state->restore_recent_root_folders(entries);

	should::is_equal(3, static_cast<int>(state->recent_root_folders().size()), "all entries restored");
	should::is_equal("c:\\newest", state->recent_root_folders()[0].view(), "most recent stays first");
	should::is_equal("c:\\middle", state->recent_root_folders()[1].view(), "middle entry keeps position");
	should::is_equal("c:\\oldest", state->recent_root_folders()[2].view(), "oldest stays last");
	should::is_equal("c:\\newest\\a.md", state->recent_root_document(pf::file_path{"c:\\newest"}).view(),
	                 "document restored alongside its folder");
}

static void should_restore_last_open_file_when_switching_recent_root_folder()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	const auto first_root = create_temp_test_root();
	const auto second_root = create_temp_test_root();
	const auto first_file = first_root.combine("alpha", "md");
	const auto second_file = second_root.combine("beta", "md");

	write_test_text_file(first_file, "alpha");
	write_test_text_file(second_file, "beta");

	state->refresh_index(first_root);
	state->load_doc(first_file);
	state->refresh_index(second_root);
	state->load_doc(second_file);

	const auto menu = state->build_recent_root_folder_menu();
	should::is_equal_true(menu.size() >= 2, "recent root menu contains both roots");
	menu[1].action();

	should::is_equal(first_root.view(), state->root_item()->path.view(), "recent folder switch restored root");
	should::is_equal(first_file.view(), state->active_item()->path.view(),
	                 "recent folder switch restored last open file");
	should::is_equal_true(find_test_item(state->root_item(), second_file) == nullptr,
	                      "recent folder switch does not carry old root file into new tree");

	pf::platform_recycle_file(first_root);
	pf::platform_recycle_file(second_root);
}

static void should_select_search_match_after_deferred_load()
{
	const auto root = create_temp_test_root();
	const auto file = root.combine("match", "txt");
	write_test_text_file(file, "alpha\r\nbeta needle gamma\r\n");

	const auto scheduler = std::make_shared<deferred_scheduler>();
	const auto state = std::make_shared<app_state>(scheduler);
	state->_startup_folder = root;
	state->on_create(std::make_shared<stub_window_frame>());
	scheduler->pump();

	const auto item = find_test_item(state->root_item(), file);
	should::is_equal_true(item != nullptr, "indexed file found");

	// "needle" begins at byte 5 of line 1 and the document has not been read yet
	state->open_path_and_select(item, 1, 5, 6);
	should::is_equal_true(state->active_item()->doc->size() == 1, "document not yet loaded");

	scheduler->pump();

	const auto sel = state->active_item()->doc->selection();
	should::is_equal(1, sel._start.y, "match line selected after load");
	should::is_equal(5, sel._start.x, "match start selected after load");
	should::is_equal(11, sel._end.x, "match end selected after load");

	pf::platform_recycle_file(root);
}

static void should_scroll_to_search_match_after_deferred_load()
{
	const auto root = create_temp_test_root();
	const auto file = root.combine("scroll", "txt");

	std::string text;
	for (int i = 0; i < 200; i++) text += std::format("line {}\r\n", i);
	text += "beta needle gamma\r\n";
	write_test_text_file(file, text);

	const auto scheduler = std::make_shared<deferred_scheduler>();
	const auto state = std::make_shared<app_state>(scheduler);
	state->_startup_folder = root;
	state->on_create(std::make_shared<stub_window_frame>());
	scheduler->pump();

	const auto item = find_test_item(state->root_item(), file);
	should::is_equal_true(item != nullptr, "indexed file found");

	state->open_path_and_select(item, 200, 5, 6);
	scheduler->pump();

	// The view metrics still described the placeholder document, so the scroll clamped to zero
	should::is_equal_true(state->_doc_view->scroll_line() > 0, "view scrolled to the match");

	pf::platform_recycle_file(root);
}

static void should_doc_is_json()
{
	const auto d1 = std::make_shared<document>(null_ev, "{\"key\":\"value\"}");
	should::is_equal_true(d1->is_json());

	const auto d2 = std::make_shared<document>(null_ev, "hello world");
	should::is_equal(false, d2->is_json());

	const auto d3 = std::make_shared<document>(null_ev, "  \t{}");
	should::is_equal_true(d3->is_json());
}


static void should_doc_sort_remove_duplicates()
{
	const auto d = std::make_shared<document>(null_ev,
	                                          "banana\napple\nbanana\ncherry\napple");
	d->sort_remove_duplicates();

	should::is_equal("apple\nbanana\ncherry", d->str());
}

static void should_doc_sort_remove_duplicates_keeps_case()
{
	const auto d = std::make_shared<document>(null_ev,
	                                          "Apple\napple\nApple\nbanana");
	d->sort_remove_duplicates();

	should::is_equal("Apple\napple\nbanana", d->str());
}

static void should_save_preserves_bom_presence()
{
	const auto root = create_temp_test_root();

	const auto no_bom_path = root.combine("no_bom.txt");
	write_test_text_file(no_bom_path, "hello\r\nworld");

	const auto without_bom = std::make_shared<document>(null_ev);
	without_bom->apply_loaded_data(no_bom_path, load_lines(no_bom_path));
	should::is_equal_true(without_bom->save_to_file(no_bom_path), "saved BOM-less file");
	should::is_equal("hello\r\nworld", read_test_text_file(no_bom_path), "no BOM added on save");

	const auto bom_path = root.combine("bom.txt");
	write_test_text_file(bom_path, "\xEF\xBB\xBF" "hello");

	const auto with_bom = std::make_shared<document>(null_ev);
	with_bom->apply_loaded_data(bom_path, load_lines(bom_path));
	should::is_equal_true(with_bom->save_to_file(bom_path), "saved BOM file");
	should::is_equal("\xEF\xBB\xBF" "hello", read_test_text_file(bom_path), "BOM preserved on save");

	pf::platform_recycle_file(root);
}

// A UTF-16 file used to come back as UTF-8, silently changing the encoding
static void should_save_preserves_utf16_encoding()
{
	const auto root = create_temp_test_root();
	const auto path = root.combine("utf16.txt");

	// FF FE BOM followed by "hi" in UTF-16 LE
	write_test_text_file(path, std::string_view("\xFF\xFE\x68\x00\x69\x00", 6));

	const auto d = std::make_shared<document>(null_ev);
	d->apply_loaded_data(path, load_lines(path));

	should::is_equal(static_cast<int>(file_encoding::utf16), static_cast<int>(d->encoding()),
	                 "loaded as UTF-16");
	should::is_equal("hi", d->str(), "decoded to UTF-8 in memory");

	should::is_equal_true(d->save_to_file(path), "saved UTF-16 file");
	should::is_equal(std::string("\xFF\xFE\x68\x00\x69\x00", 6), read_test_text_file(path),
	                 "written back as UTF-16 with its BOM");

	pf::platform_recycle_file(root);
}

static void should_doc_reformat_json()
{
	const auto d = std::make_shared<document>(null_ev, "{\"a\":\"b\"}");
	d->reformat_json();

	// After reformat, the doc should contain formatted JSON with braces on separate lines
	const auto result = d->str();
	should::is_equal_true(result.find(u8'{') != std::string::npos, "json has open brace");
	should::is_equal_true(result.find(u8'}') != std::string::npos, "json has close brace");
	// The key-value should have spaces around colon
	should::is_equal_true(result.find(" : ") != std::string::npos, "json has spaced colon");
}

static void should_undo_back_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, "hello");
	should::is_equal(false, d->is_modified());

	// Make an edit
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(5, 0), " world");
	}
	should::is_equal_true(d->is_modified());

	// Undo → back to saved state
	d->edit_undo();
	should::is_equal(false, d->is_modified(), "undo to clean");

	// Redo → modified again
	d->edit_redo();
	should::is_equal_true(d->is_modified(), "redo is modified");

	// Undo again
	d->edit_undo();
	should::is_equal(false, d->is_modified(), "undo again to clean");
}

static void should_undo_multiple_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, "abc");

	// Two edits
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(3, 0), "d");
	}
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(4, 0), "e");
	}
	should::is_equal_true(d->is_modified());

	// Undo one → still modified
	d->edit_undo();
	should::is_equal_true(d->is_modified(), "undo one still modified");

	// Undo two → clean
	d->edit_undo();
	should::is_equal(false, d->is_modified(), "undo two clean");
}

static void should_undo_delete_back_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, "hello world");
	should::is_equal(false, d->is_modified());

	{
		undo_group ug(d);
		d->delete_text(ug, text_selection(5, 0, 11, 0));
	}
	should::is_equal("hello", d->str());
	should::is_equal_true(d->is_modified(), "delete is modified");

	d->edit_undo();
	should::is_equal("hello world", d->str(), "undo restores deleted text");
	should::is_equal(false, d->is_modified(), "undo of delete returns to clean");

	d->edit_redo();
	should::is_equal_true(d->is_modified(), "redo is modified");
}


// ── Search model tests ─────────────────────────────────────────────────────────

static void should_search_doc_basic()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{"test.txt"}, "test.txt", false,
	                                               std::make_shared<document>(
		                                               null_ev, "hello world\ngoodbye world\nhello again"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("hello");

	should::is_equal(2, static_cast<int>(item->search_results.size()), "search result count");
	should::is_equal(0, item->search_results[0].line_number, "first result line");
	should::is_equal(2, item->search_results[1].line_number, "second result line");
}

static void should_search_doc_match_positions()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{"test.txt"}, "test.txt", false,
	                                               std::make_shared<document>(null_ev, "\tone two one"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("one");

	should::is_equal(2, static_cast<int>(item->search_results.size()), "match count");

	// First match at position 1 (after tab), trimmed text starts at 0
	should::is_equal(1, item->search_results[0].line_match_pos, "first match pos");
	should::is_equal(0, item->search_results[0].text_match_start, "first trimmed pos");
	should::is_equal(3, item->search_results[0].text_match_length, "first match len");

	// Second match
	should::is_equal(9, item->search_results[1].line_match_pos, "second match pos");
	should::is_equal(8, item->search_results[1].text_match_start, "second trimmed pos");
}

static void should_clip_context_of_a_very_long_result_line()
{
	const auto state = create_test_app();

	auto line = std::string(4000, 'a');
	line.replace(3000, 6, "needle");

	const auto item = std::make_shared<index_item>(pf::file_path{"long.txt"}, "long.txt", false,
	                                               std::make_shared<document>(null_ev, line));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("needle");

	should::is_equal(1, static_cast<int>(item->search_results.size()), "match count");

	const auto& result = item->search_results.front();
	should::is_equal_true(result.line_text.size() <= 400, "context clipped to a bounded window");
	should::is_equal(3000, result.line_match_pos, "match position within the whole line is kept");
	should::is_equal("needle", result.line_text.substr(result.text_match_start, 6),
	                 "match offset still points at the match");
}

// ── Word wrap tests ────────────────────────────────────────────────────────────
//
// Structural edits splice the per-line wrap arrays instead of rebuilding them, so
// every case here checks the spliced result against a forced full rebuild.

static std::shared_ptr<app_state> create_wrapped_test_app()
{
	const auto state = create_test_app();
	pf::window_frame_ptr window = std::make_shared<stub_window_frame>();
	stub_measure_context measure;

	state->set_word_wrap(true);
	state->_doc_view->handle_size(window, pf::isize{400, 320}, measure);

	const auto doc = state->doc();
	undo_group ug(doc);
	doc->insert_text(ug, text_location(0, 0),
	                 "the quick brown fox jumps over the lazy dog and keeps on running for a while\n"
	                 "short\n"
	                 "another long line that will certainly need to wrap more than once inside this view\n"
	                 "\n"
	                 "a final line that is also long enough to wrap onto more than a single visual row");
	return state;
}

static std::string wrap_shape(const std::shared_ptr<app_state>& state)
{
	std::string shape;
	for (int i = 0; i < static_cast<int>(state->doc()->size()); i++)
		shape += std::format("{},", state->_doc_view->line_visual_rows(i));
	return shape;
}

static void should_match_a_full_rebuild(const std::shared_ptr<app_state>& state, const std::string_view message)
{
	state->_doc_view->layout();
	const auto spliced = wrap_shape(state);

	state->_doc_view->mark_wrap_dirty_all();
	state->_doc_view->layout();

	should::is_equal(wrap_shape(state), spliced, message);
}

static void should_wrap_long_lines_onto_several_rows()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	should::is_equal_true(state->_doc_view->line_visual_rows(0) > 1, "a long line occupies several rows");
	should::is_equal(1, state->_doc_view->line_visual_rows(1), "a short line occupies one row");
	should::is_equal(1, state->_doc_view->line_visual_rows(3), "an empty line occupies one row");
}

static void should_splice_wrap_when_a_line_is_split()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();
	const auto long_line_rows = state->_doc_view->line_visual_rows(2);
	should::is_equal_true(long_line_rows > 1, "the line below the edit wraps");

	undo_group ug(doc);
	doc->insert_text(ug, text_location(20, 0), u8'\n');

	should::is_equal(6, static_cast<int>(doc->size()), "a line was added");

	// Before any re-layout the tail has already moved down a row, which it could only
	// do if the arrays were spliced rather than thrown away and rebuilt
	should::is_equal(long_line_rows, state->_doc_view->line_visual_rows(3),
	                 "the untouched line kept its rows at its new index");

	should_match_a_full_rebuild(state, "wrap after splitting a line");
}

static void should_splice_wrap_when_lines_are_joined()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();
	const auto last_line_rows = state->_doc_view->line_visual_rows(4);
	should::is_equal_true(last_line_rows > 1, "the last line wraps");

	undo_group ug(doc);
	doc->delete_text(ug, text_location(0, 2));

	should::is_equal(4, static_cast<int>(doc->size()), "a line was removed");
	should::is_equal(last_line_rows, state->_doc_view->line_visual_rows(3),
	                 "the untouched last line kept its rows at its new index");

	should_match_a_full_rebuild(state, "wrap after joining two lines");
}

static void should_splice_wrap_when_a_block_is_deleted()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();
	const auto last_line_rows = state->_doc_view->line_visual_rows(4);

	undo_group ug(doc);
	doc->delete_text(ug, text_selection(text_location(10, 0), text_location(4, 3)));

	should::is_equal(2, static_cast<int>(doc->size()), "three lines were removed");
	should::is_equal(last_line_rows, state->_doc_view->line_visual_rows(1),
	                 "the untouched last line kept its rows at its new index");

	should_match_a_full_rebuild(state, "wrap after deleting a multi-line selection");
}

static void should_splice_wrap_when_a_block_is_inserted()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();
	const auto last_line_rows = state->_doc_view->line_visual_rows(4);

	{
		undo_group ug(doc);
		doc->insert_text(ug, text_location(5, 1),
		                 "\nan inserted line long enough that it has to wrap across more than one row\nand another\n");
	}

	should::is_equal(8, static_cast<int>(doc->size()), "three lines were added");
	should::is_equal(last_line_rows, state->_doc_view->line_visual_rows(7),
	                 "the untouched last line kept its rows at its new index");

	should_match_a_full_rebuild(state, "wrap after inserting a block");
}

static void should_splice_wrap_when_an_edit_is_undone()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();
	{
		undo_group ug(doc);
		doc->insert_text(ug, text_location(20, 0), u8'\n');
	}
	state->_doc_view->layout();

	doc->undo();

	should::is_equal(5, static_cast<int>(doc->size()), "the split was undone");
	should_match_a_full_rebuild(state, "wrap after undo");
}

static void should_rewrap_a_dirty_line_that_a_later_split_moved()
{
	const auto state = create_wrapped_test_app();
	state->_doc_view->layout();

	const auto doc = state->doc();

	// Two edits reach the view before layout runs, so the dirty line index recorded
	// by the first is in the old numbering by the time the second has moved that line
	{
		undo_group ug(doc);
		doc->insert_text(ug, text_location(0, 4), "extra words that push this line onto one more visual row ");
		doc->insert_text(ug, text_location(0, 0), u8'\n');
	}

	should::is_equal(6, static_cast<int>(doc->size()), "a line was added");
	should_match_a_full_rebuild(state, "wrap after an edit below a later split");
}

static void should_search_doc_case_insensitive()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{"test.txt"}, "test.txt", false,
	                                               std::make_shared<document>(null_ev, "Hello HELLO hello"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("hello");

	should::is_equal(3, static_cast<int>(item->search_results.size()), "case insensitive count");
}

static void should_search_doc_empty_clears()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{"test.txt"}, "test.txt", false,
	                                               std::make_shared<document>(null_ev, "hello world"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("hello");
	should::is_equal(1, static_cast<int>(item->search_results.size()), "before clear");

	state->execute_search("");
	should::is_equal(0, static_cast<int>(item->search_results.size()), "after clear");
}

static void should_search_multiple_files()
{
	const auto state = create_test_app();

	const auto item1 = std::make_shared<index_item>(pf::file_path{"a.txt"}, "a.txt", false,
	                                                std::make_shared<document>(null_ev, "foo bar"));
	const auto item2 = std::make_shared<index_item>(pf::file_path{"b.txt"}, "b.txt", false,
	                                                std::make_shared<document>(null_ev, "bar baz"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item1);
	root->children.push_back(item2);
	state->set_root(root);

	state->execute_search("bar");

	should::is_equal(1, static_cast<int>(item1->search_results.size()), "file1 results");
	should::is_equal(1, static_cast<int>(item2->search_results.size()), "file2 results");
}

static void should_search_no_match()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{"test.txt"}, "test.txt", false,
	                                               std::make_shared<document>(null_ev, "hello world"));

	const auto root = std::make_shared<index_item>(pf::file_path{"root"}, "root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search("xyz");

	should::is_equal(0, static_cast<int>(item->search_results.size()), "no match");
}

static void should_json_round_trip()
{
	constexpr std::string_view text =
		R"({"id":7,"ok":true,"none":null,"ratio":0.5,"tags":["a","b"],"nested":{"deep":[1,2,3]}})";

	const auto parsed = json::parse(text);
	should::is_equal_true(parsed.ok, "parsed");
	should::is_equal(text, parsed.root.to_string(), "round trip");
}

static void should_json_read_members()
{
	const auto parsed = json::parse(R"({"jsonrpc":"2.0","id":42,"params":{"sessionId":"s1"}})");
	should::is_equal_true(parsed.ok, "parsed");

	const auto& root = parsed.root;
	should::is_equal("2.0", root["jsonrpc"].text(), "string member");
	should::is_equal(42, static_cast<int>(root["id"].integer()), "integer member");
	should::is_equal("s1", root["params"]["sessionId"].text(), "nested member");
	should::is_equal_true(root.contains("params"), "contains");
	should::is_equal(size_t{3}, root.size(), "member count");
}

// Accessors are total so protocol handling never needs a type check before a read
static void should_json_accessors_fall_back()
{
	const auto parsed = json::parse(R"({"n":1})");
	should::is_equal_true(parsed.ok, "parsed");

	const auto& root = parsed.root;
	should::is_equal_true(root["missing"].is_null(), "missing key is null");
	should::is_equal("fallback", root["missing"].text("fallback"), "missing text");
	should::is_equal(9, static_cast<int>(root["missing"].integer(9)), "missing integer");
	should::is_equal_true(root["n"].boolean(true), "wrong type keeps fallback");
	should::is_equal("", root["n"].text(), "number as text is empty");
	should::is_equal_true(root[7].is_null(), "index on object is null");
}

static void should_json_parse_escapes()
{
	const auto parsed = json::parse(R"(["a\"b","c\\d","e\/f","g\nh","\u0041\u00e9\u4e2d","\ud83d\ude00"])");
	should::is_equal_true(parsed.ok, "parsed");

	const auto& a = parsed.root;
	should::is_equal("a\"b", a[0].text(), "quote");
	should::is_equal("c\\d", a[1].text(), "backslash");
	should::is_equal("e/f", a[2].text(), "solidus");
	should::is_equal("g\nh", a[3].text(), "newline");
	should::is_equal("A\xC3\xA9\xE4\xB8\xAD", a[4].text(), "bmp escapes as utf8");
	should::is_equal("\xF0\x9F\x98\x80", a[5].text(), "surrogate pair as utf8");
}

// A lone surrogate cannot be encoded, so it becomes U+FFFD rather than failing the message
static void should_json_replace_lone_surrogates()
{
	const auto lead = json::parse(R"(["\ud83d"])");
	should::is_equal_true(lead.ok, "lead parsed");
	should::is_equal("\xEF\xBF\xBD", lead.root[0].text(), "lone lead");

	const auto trail = json::parse(R"(["\udc00"])");
	should::is_equal_true(trail.ok, "trail parsed");
	should::is_equal("\xEF\xBF\xBD", trail.root[0].text(), "lone trail");

	const auto unpaired = json::parse(R"(["\ud83dZ"])");
	should::is_equal_true(unpaired.ok, "unpaired parsed");
	should::is_equal("\xEF\xBF\xBDZ", unpaired.root[0].text(), "lead then plain text");
}

// The transport is newline-delimited, so a serialised message must never contain a raw newline
static void should_json_escape_control_characters()
{
	auto v = json::object();
	v.set("text", std::string("line1\nline2\r\ttab\b\f\x01 end"));

	const auto text = v.to_string();
	should::is_equal(R"({"text":"line1\nline2\r\ttab\b\f\u0001 end"})", text, "escaped");
	should::is_equal_true(text.find('\n') == std::string::npos, "no raw newline");

	const auto parsed = json::parse(text);
	should::is_equal_true(parsed.ok, "reparsed");
	should::is_equal("line1\nline2\r\ttab\b\f\x01 end", parsed.root["text"].text(), "value survives");
}

static void should_json_keep_integers_exact()
{
	const auto parsed = json::parse(R"([0,-1,7,9007199254740993,-9007199254740993])");
	should::is_equal_true(parsed.ok, "parsed");
	should::is_equal("[0,-1,7,9007199254740993,-9007199254740993]", parsed.root.to_string(),
	                 "large ids survive as integers");

	const auto real = json::parse("1.5");
	should::is_equal_true(real.ok, "real parsed");
	should::is_equal("1.5", real.root.to_string(), "real round trip");

	const auto exponent = json::parse("2e3");
	should::is_equal_true(exponent.ok, "exponent parsed");
	should::is_equal(2000, static_cast<int>(exponent.root.integer()), "exponent value");
}

static void should_json_reject_bad_numbers()
{
	constexpr std::string_view bad[] = {
		"01", "-", "+1", ".5", "1.", "1.e3", "1e", "1e+", "--1", "0x10", "nan", "inf", "Infinity"
	};

	for (const auto& text : bad)
	{
		const auto parsed = json::parse(text);
		should::is_equal(false, parsed.ok, std::format("rejects '{}'", text));
	}

	// Out of range rather than silently becoming infinity
	should::is_equal(false, json::parse("1e309").ok, "rejects overflow");

	// Too large for int64, so it degrades to a double rather than failing
	const auto huge = json::parse("99999999999999999999");
	should::is_equal_true(huge.ok, "huge parsed");
	should::is_equal_true(huge.root.is_number(), "huge is number");
}

static void should_json_reject_malformed_input()
{
	constexpr std::string_view bad[] = {
		"", "   ", "{", "}", "[", "]", "[1,]", "{\"a\":}", "{\"a\" 1}", "{a:1}", "{\"a\":1,}",
		"\"unterminated", "\"bad\\escape\"", "tru", "nulll", "[1] extra", "{}{}", "\"\x01\""
	};

	for (const auto& text : bad)
	{
		const auto parsed = json::parse(text);
		should::is_equal(false, parsed.ok, std::format("rejects '{}'", text));
		should::is_equal_true(!parsed.error.empty(), "reports an error");
		should::is_equal_true(parsed.root.is_null(), "yields null on failure");
	}
}

// A hostile message must not be able to exhaust the stack
static void should_json_limit_nesting_depth()
{
	const auto build = [](const int depth)
	{
		return std::string(depth, '[') + std::string(depth, ']');
	};

	should::is_equal_true(json::parse(build(json::max_parse_depth - 1)).ok, "accepts allowed depth");
	should::is_equal(false, json::parse(build(json::max_parse_depth + 10)).ok, "rejects excessive depth");
	should::is_equal(false, json::parse(build(10000)).ok, "rejects extreme depth");
}

static void should_json_build_messages()
{
	auto params = json::object();
	params.set("sessionId", "abc");
	params.set("prompt", json::array().add(json::object().set("type", "text").set("text", "hi")));

	auto request = json::object();
	request.set("jsonrpc", "2.0");
	request.set("id", 1);
	request.set("method", "session/prompt");
	request.set("params", std::move(params));

	should::is_equal(
		R"({"jsonrpc":"2.0","id":1,"method":"session/prompt","params":{"sessionId":"abc","prompt":[{"type":"text","text":"hi"}]}})",
		request.to_string(), "built message");
}

// Members keep insertion order, and a repeated key updates in place rather than appending
static void should_json_replace_duplicate_keys()
{
	auto v = json::object();
	v.set("a", 1);
	v.set("b", 2);
	v.set("a", 3);

	should::is_equal(size_t{2}, v.size(), "no duplicate member");
	should::is_equal(R"({"a":3,"b":2})", v.to_string(), "updated in place");

	const auto parsed = json::parse(R"({"a":1,"a":2})");
	should::is_equal_true(parsed.ok, "parsed");
	should::is_equal(2, static_cast<int>(parsed.root["a"].integer()), "last wins");
}

static void should_json_pass_through_utf8()
{
	// U+00E9 arrives escaped, the rest as raw UTF-8 bytes
	const std::string text = "{\"s\":\"\\u00e9 \xE4\xB8\xAD \xF0\x9F\x98\x80\"}";
	const auto parsed = json::parse(text);
	should::is_equal_true(parsed.ok, "parsed");

	// Non-ASCII is emitted raw, not re-escaped
	should::is_equal("{\"s\":\"\xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80\"}", parsed.root.to_string(), "raw utf8 out");
}

static std::vector<std::string> split_stream(const std::vector<std::string_view>& chunks,
                                             const size_t max_line_bytes = pf::line_splitter::default_max_line_bytes)
{
	pf::line_splitter splitter;
	splitter.max_line_bytes = max_line_bytes;

	std::vector<std::string> lines;
	const auto emit = [&lines](const std::string_view line) { lines.emplace_back(line); };

	for (const auto& chunk : chunks)
		splitter.feed(chunk, emit);

	splitter.flush(emit);
	return lines;
}

// A pipe read boundary can fall anywhere, including inside a multi-byte character
static void should_split_lines_across_read_boundaries()
{
	const auto whole = split_stream({"one\ntwo\nthree\n"});
	should::is_equal(size_t{3}, whole.size(), "three lines");
	should::is_equal("one", whole[0], "first");
	should::is_equal("three", whole[2], "third");

	const auto split = split_stream({"on", "e\ntw", "o\nthr", "ee\n"});
	should::is_equal(size_t{3}, split.size(), "three lines when split");
	should::is_equal("one", split[0], "first when split");
	should::is_equal("two", split[1], "second when split");
	should::is_equal("three", split[2], "third when split");

	const auto by_byte = split_stream({"a", "b", "\n", "c", "\n"});
	should::is_equal(size_t{2}, by_byte.size(), "byte at a time");
	should::is_equal("ab", by_byte[0], "byte at a time first");

	const auto utf8 = split_stream({"caf\xC3", "\xA9\n"});
	should::is_equal(size_t{1}, utf8.size(), "one line");
	should::is_equal("caf\xC3\xA9", utf8[0], "codepoint spanning a boundary");
}

static void should_split_lines_handle_endings_and_blanks()
{
	const auto crlf = split_stream({"one\r\ntwo\r\n"});
	should::is_equal(size_t{2}, crlf.size(), "crlf line count");
	should::is_equal("one", crlf[0], "carriage return stripped");

	const auto blanks = split_stream({"\n\na\n"});
	should::is_equal(size_t{3}, blanks.size(), "blank lines kept");
	should::is_equal("", blanks[0], "first blank");
	should::is_equal("a", blanks[2], "third");

	// A stream that ends without a newline still yields its last record
	const auto partial = split_stream({"tail"});
	should::is_equal(size_t{1}, partial.size(), "partial flushed");
	should::is_equal("tail", partial[0], "partial content");

	should::is_equal(size_t{0}, split_stream({""}).size(), "empty stream");
}

// A stream that never sends a newline must not grow the buffer without bound
static void should_split_lines_discard_oversized_records()
{
	const std::string huge(64, 'x');
	const auto lines = split_stream({huge, huge, "\nafter\n"}, 32);

	should::is_equal(size_t{1}, lines.size(), "oversized record dropped");
	should::is_equal("after", lines[0], "resynced at the next newline");

	// The record is dropped whole, never truncated into a partial one
	const auto joined = split_stream({"12345678\nshort\n"}, 6);
	should::is_equal(size_t{1}, joined.size(), "only the short record survives");
	should::is_equal("short", joined[0], "short record");
}

static void should_quote_command_arguments()
{
	should::is_equal("simple", pf::quote_command_arg("simple"), "no quoting needed");
	should::is_equal("--acp", pf::quote_command_arg("--acp"), "option");
	should::is_equal("\"\"", pf::quote_command_arg(""), "empty argument");
	should::is_equal("\"a b\"", pf::quote_command_arg("a b"), "space");
	should::is_equal("\"a\\\"b\"", pf::quote_command_arg("a\"b"), "embedded quote");
	should::is_equal("C:\\path\\file", pf::quote_command_arg("C:\\path\\file"), "backslashes kept");
	should::is_equal("\"C:\\my path\\\\\"", pf::quote_command_arg("C:\\my path\\"), "trailing backslash doubled");
	should::is_equal("\"a\\\\\\\"b\"", pf::quote_command_arg("a\\\"b"), "backslash before quote");
}

static void should_detect_shell_metacharacters()
{
	should::is_equal(false, pf::has_shell_metacharacter("--acp"), "plain option");
	should::is_equal(false, pf::has_shell_metacharacter("C:\\path\\file.txt"), "plain path");
	should::is_equal_true(pf::has_shell_metacharacter("a & b"), "ampersand");
	should::is_equal_true(pf::has_shell_metacharacter("a | b"), "pipe");
	should::is_equal_true(pf::has_shell_metacharacter("%PATH%"), "environment expansion");
	should::is_equal_true(pf::has_shell_metacharacter("a > b"), "redirect");
	should::is_equal_true(pf::has_shell_metacharacter("a\nb"), "newline");
}

// recording_transport — Captures what the client sends and lets a test reply as the agent
struct recording_transport final : acp::transport
{
	std::vector<std::string> sent;
	bool fail_sends = false;

	bool send_line(const std::string_view line) override
	{
		if (fail_sends)
			return false;

		sent.emplace_back(line);
		return true;
	}

	[[nodiscard]] json::value last() const
	{
		return sent.empty() ? json::value() : json::parse(sent.back()).root;
	}

	[[nodiscard]] json::value message(const size_t index) const
	{
		return index < sent.size() ? json::parse(sent[index]).root : json::value();
	}

	[[nodiscard]] int64_t last_id() const { return last()["id"].integer(); }
};

// Drives the handshake so a test can start from a ready session
static void complete_handshake(acp::client& client, recording_transport& wire,
                               const std::string_view session_id = "sess-1")
{
	client.start("C:\\work");

	client.on_line(std::format(
		R"({{"jsonrpc":"2.0","id":{},"result":{{"protocolVersion":1,"agentCapabilities":{{"loadSession":true}}}}}})",
		wire.last_id()));

	client.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"sessionId":"{}"}}}})",
	                           wire.last_id(), session_id));
}

static void should_acp_complete_handshake()
{
	recording_transport wire;
	acp::client client(wire);

	auto ready_count = 0;
	client.on_ready = [&ready_count] { ++ready_count; };

	client.start("C:\\work", {.read_text_file = true, .write_text_file = true});

	const auto init = wire.message(0);
	should::is_equal("2.0", init["jsonrpc"].text(), "jsonrpc version");
	should::is_equal("initialize", init["method"].text(), "first method");
	should::is_equal(acp::protocol_version, static_cast<int>(init["params"]["protocolVersion"].integer()),
	                 "protocol version");
	should::is_equal_true(init["params"]["clientCapabilities"]["fs"]["readTextFile"].boolean(),
	                      "advertises reads");
	should::is_equal(false, init["params"]["clientCapabilities"]["terminal"].boolean(true),
	                 "declines terminal");

	client.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"protocolVersion":1}}}})",
	                           init["id"].integer()));

	const auto new_session = wire.message(1);
	should::is_equal("session/new", new_session["method"].text(), "second method");
	should::is_equal("C:\\work", new_session["params"]["cwd"].text(), "working directory");

	should::is_equal(0, ready_count, "not ready until the session exists");

	client.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"sessionId":"abc"}}}})",
	                           new_session["id"].integer()));

	should::is_equal(1, ready_count, "ready once");
	should::is_equal_true(client.ready(), "state is ready");
	should::is_equal("abc", client.session_id(), "session id");
}

static void should_acp_report_handshake_failure()
{
	recording_transport wire;
	acp::client client(wire);

	std::string reported;
	client.on_error = [&reported](const std::string_view text) { reported = text; };

	client.start("C:\\work");
	client.on_line(std::format(
		R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32000,"message":"not logged in"}}}})", wire.last_id()));

	should::is_equal(false, client.ready(), "not ready");
	should::is_equal_true(reported.find("not logged in") != std::string::npos, "reports the agent message");

	// A session with no id is a failure even though the call succeeded
	recording_transport wire2;
	acp::client client2(wire2);
	client2.on_error = [&reported](const std::string_view text) { reported = text; };
	client2.start("C:\\work");
	client2.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{}}}})", wire2.last_id()));
	client2.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{}}}})", wire2.last_id()));

	should::is_equal(false, client2.ready(), "no session id means not ready");
}

static void should_acp_send_prompt_and_end_turn()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	auto ended = 0;
	auto reason = acp::stop_reason::unknown;
	client.on_turn_end = [&](const acp::stop_reason r)
	{
		++ended;
		reason = r;
	};

	should::is_equal_true(client.send_prompt("fix the bug"), "prompt sent");
	should::is_equal_true(client.turn_in_flight(), "turn in flight");

	const auto prompt = wire.last();
	should::is_equal("session/prompt", prompt["method"].text(), "method");
	should::is_equal("sess-1", prompt["params"]["sessionId"].text(), "session id");
	should::is_equal("text", prompt["params"]["prompt"][0]["type"].text(), "content type");
	should::is_equal("fix the bug", prompt["params"]["prompt"][0]["text"].text(), "content text");

	// A second prompt is refused while one is in flight
	should::is_equal(false, client.send_prompt("another"), "one turn at a time");

	client.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"stopReason":"end_turn"}}}})",
	                           prompt["id"].integer()));

	should::is_equal(1, ended, "turn ended once");
	should::is_equal_true(reason == acp::stop_reason::end_turn, "end_turn");
	should::is_equal(false, client.turn_in_flight(), "no longer in flight");
	should::is_equal_true(client.send_prompt("another"), "next turn allowed");
}

static void should_acp_refuse_prompt_before_ready()
{
	recording_transport wire;
	acp::client client(wire);

	should::is_equal(false, client.send_prompt("too early"), "refused before start");
	should::is_equal(size_t{0}, wire.sent.size(), "nothing sent");

	client.start("C:\\work");
	should::is_equal(false, client.send_prompt("still too early"), "refused mid handshake");
	should::is_equal(size_t{1}, wire.sent.size(), "only the initialize");
}

static void should_acp_cancel_turn()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	client.cancel();
	should::is_equal(size_t{2}, wire.sent.size(), "nothing to cancel");

	should::is_equal_true(client.send_prompt("long task"), "prompt sent");
	const auto prompt_id = wire.last_id();
	client.cancel();
	const auto cancel = wire.last();
	should::is_equal("session/cancel", cancel["method"].text(), "cancel method");
	should::is_equal("sess-1", cancel["params"]["sessionId"].text(), "cancel session");
	should::is_equal_true(cancel["id"].is_null(), "cancel is a notification");

	auto reason = acp::stop_reason::unknown;
	client.on_turn_end = [&reason](const acp::stop_reason r) { reason = r; };

	client.on_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"stopReason":"cancelled"}}}})", prompt_id));
	should::is_equal_true(reason == acp::stop_reason::cancelled, "cancelled reason");
}

static void should_acp_dispatch_session_updates()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	std::vector<std::string> chunks;
	client.on_session_update = [&chunks](const json::value& params)
	{
		chunks.emplace_back(params["update"]["content"]["text"].text());
	};

	client.on_line(
		R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Hel"}}}})");
	client.on_line(
		R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"lo"}}}})");

	should::is_equal(size_t{2}, chunks.size(), "two chunks");
	should::is_equal("Hel", chunks[0], "first chunk");
	should::is_equal("lo", chunks[1], "second chunk");

	// An unknown notification is ignored rather than treated as an error
	auto errors = 0;
	client.on_error = [&errors](std::string_view) { ++errors; };
	client.on_line(R"({"jsonrpc":"2.0","method":"session/unheard_of","params":{}})");
	should::is_equal(0, errors, "unknown notification ignored");
}

static void should_acp_answer_permission_requests()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	acp::request_id received = 0;
	std::string tool;
	client.on_permission_request = [&](const acp::request_id id, const json::value& params)
	{
		received = id;
		tool = params["toolCall"]["title"].text();
	};

	client.on_line(
		R"({"jsonrpc":"2.0","id":"req-7","method":"session/request_permission","params":{"sessionId":"sess-1","toolCall":{"title":"git status"},"options":[{"optionId":"allow","name":"Allow"}]}})");

	should::is_equal_true(received != 0, "handler called");
	should::is_equal("git status", tool, "tool title");
	should::is_equal(size_t{1}, client.pending_reply_count(), "awaiting our answer");

	auto outcome = json::object();
	outcome.set("outcome", "selected");
	outcome.set("optionId", "allow");
	client.respond(received, json::object().set("outcome", std::move(outcome)));

	const auto reply = wire.last();
	should::is_equal("req-7", reply["id"].text(), "string id echoed verbatim");
	should::is_equal("allow", reply["result"]["outcome"]["optionId"].text(), "selected option");
	should::is_equal(size_t{0}, client.pending_reply_count(), "no longer awaiting");

	// Answering twice sends nothing more
	const auto count = wire.sent.size();
	client.respond(received, json::object());
	should::is_equal(count, wire.sent.size(), "second answer ignored");
}

// An unimplemented method must still get a reply, or the agent waits forever
static void should_acp_reject_unknown_requests()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	client.on_line(R"({"jsonrpc":"2.0","id":31,"method":"terminal/create","params":{}})");

	const auto reply = wire.last();
	should::is_equal(31, static_cast<int>(reply["id"].integer()), "id echoed");
	should::is_equal(acp::error_code::method_not_found, static_cast<int>(reply["error"]["code"].integer()),
	                 "method not found");
	should::is_equal_true(reply["error"]["message"].text().find("terminal/create") != std::string_view::npos,
	                      "names the method");
	should::is_equal(size_t{0}, client.pending_reply_count(), "nothing left outstanding");
}

static void should_acp_survive_malformed_input()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	std::vector<std::string> errors;
	client.on_error = [&errors](const std::string_view text) { errors.emplace_back(text); };

	client.on_line("");
	client.on_line("   ");
	should::is_equal(size_t{0}, errors.size(), "blank lines ignored");

	client.on_line("{not json");
	client.on_line("[1,2,3]");
	client.on_line(R"({"jsonrpc":"2.0"})");
	should::is_equal(size_t{3}, errors.size(), "each bad line reported once");

	// A response to an id we never sent is ignored, not an error
	client.on_line(R"({"jsonrpc":"2.0","id":9999,"result":{}})");
	should::is_equal(size_t{3}, errors.size(), "stale response ignored");

	// Still usable afterwards
	should::is_equal_true(client.send_prompt("still working"), "client still usable");
}

static void should_acp_fail_pending_requests_on_disconnect()
{
	recording_transport wire;
	acp::client client(wire);
	complete_handshake(client, wire);

	auto ended = 0;
	auto reason = acp::stop_reason::unknown;
	client.on_turn_end = [&](const acp::stop_reason r)
	{
		++ended;
		reason = r;
	};

	should::is_equal_true(client.send_prompt("work"), "prompt sent");
	should::is_equal(size_t{1}, client.pending_request_count(), "prompt pending");

	client.on_disconnect("the agent stopped");

	should::is_equal(1, ended, "turn ended");
	should::is_equal_true(reason == acp::stop_reason::error, "ended with an error");
	should::is_equal(size_t{0}, client.pending_request_count(), "nothing left pending");
	should::is_equal(false, client.ready(), "not ready");
	should::is_equal(false, client.turn_in_flight(), "no turn in flight");
}

static void should_acp_parse_stop_reasons()
{
	should::is_equal_true(acp::parse_stop_reason("end_turn") == acp::stop_reason::end_turn, "end_turn");
	should::is_equal_true(acp::parse_stop_reason("cancelled") == acp::stop_reason::cancelled, "cancelled");
	should::is_equal_true(acp::parse_stop_reason("refusal") == acp::stop_reason::refusal, "refusal");
	should::is_equal_true(acp::parse_stop_reason("max_tokens") == acp::stop_reason::max_tokens, "max_tokens");
	should::is_equal_true(acp::parse_stop_reason("something_new") == acp::stop_reason::unknown, "unknown");
	should::is_equal("end_turn", acp::to_string(acp::stop_reason::end_turn), "to_string");
}

static constexpr std::string_view sample_session =
	"<!-- rethinkify agent session v1 -->\n"
	"\n"
	"## Session\n"
	"- model: claude-sonnet-4.5\n"
	"- yolo: off\n"
	"\n"
	"## You\n"
	"\n"
	"Fix the wrap cache splice bug.\n"
	"\n"
	"## Agent\n"
	"\n"
	"The dirty range is still in the old numbering.\n"
	"\n"
	"### Tool: git status (approved)\n"
	"\n"
	"    M src/view_doc.h\n"
	"\n"
	"### Question: which file should I change?\n"
	"\n"
	"- [x] src/view_doc.h\n"
	"- [ ] src/document.cpp\n";

// The file is the transcript, so writing it back must not disturb a single byte
static void should_session_round_trip_exactly()
{
	constexpr std::string_view samples[] = {
		sample_session,
		"",
		"\n",
		"## You\nhello",
		"no headings at all\njust text\n",
		"## Nonsense\n### Also nonsense\n\ttabbed\n   \n## You\nreal\n",
		"## Session\n- unknown-key: keep me\n- model: gpt-5\n",
	};

	for (const auto& text : samples)
	{
		const auto lines = agent_session::to_lines(text);
		should::is_equal(text, agent_session::to_text(lines), "round trip");

		// Parsing must never rewrite anything
		const auto before = agent_session::to_text(lines);
		const auto entries = agent_session::parse(lines);
		should::is_equal(before, agent_session::to_text(lines), "parse does not mutate");
		should::is_equal_true(entries.size() <= lines.size(), "entries bounded by lines");
	}
}

static void should_session_parse_entries()
{
	const auto lines = agent_session::to_lines(sample_session);
	const auto entries = agent_session::parse(lines);

	should::is_equal(size_t{5}, entries.size(), "entry count");
	should::is_equal_true(entries[0].kind == agent_entry_kind::session, "session first");
	should::is_equal_true(entries[1].kind == agent_entry_kind::user, "then user");
	should::is_equal_true(entries[2].kind == agent_entry_kind::agent, "then agent");
	should::is_equal_true(entries[3].kind == agent_entry_kind::tool_call, "then tool");
	should::is_equal_true(entries[4].kind == agent_entry_kind::question, "then question");

	should::is_equal("git status", entries[3].title, "tool title");
	should::is_equal("approved", entries[3].status, "tool status");
	should::is_equal("which file should I change?", entries[4].title, "question title");

	should::is_equal(size_t{2}, entries[4].options.size(), "option count");
	should::is_equal("src/view_doc.h", entries[4].options[0].label, "first option");
	should::is_equal_true(entries[4].options[0].chosen, "first chosen");
	should::is_equal(false, entries[4].options[1].chosen, "second not chosen");
	should::is_equal(0, entries[4].chosen_option(), "chosen index");

	// Ranges are contiguous and cover the file after the leading header
	for (size_t i = 1; i < entries.size(); ++i)
		should::is_equal(entries[i - 1].last_line, entries[i].first_line, "entries are contiguous");

	should::is_equal(static_cast<int>(lines.size()), entries.back().last_line, "last entry runs to the end");
}

// An agent writes markdown, so its own headings must not be mistaken for entry boundaries
static void should_session_keep_agent_markdown_inside_its_entry()
{
	const auto lines = agent_session::to_lines(
		"## Agent\n"
		"\n"
		"## Summary\n"
		"### Details\n"
		"#### Deeper\n"
		"Some prose.\n");

	const auto entries = agent_session::parse(lines);

	should::is_equal(size_t{1}, entries.size(), "one entry");
	should::is_equal_true(entries[0].kind == agent_entry_kind::agent, "agent entry");
	should::is_equal(static_cast<int>(lines.size()), entries[0].last_line, "covers every line");
}

static void should_session_escape_heading_like_body()
{
	should::is_equal("\\## You", agent_session::escape_body_line("## You"), "role heading escaped");
	should::is_equal("\\### Tool: x", agent_session::escape_body_line("### Tool: x"), "tool heading escaped");
	should::is_equal("\\### Plan", agent_session::escape_body_line("### Plan"), "plan heading escaped");
	should::is_equal("## Summary", agent_session::escape_body_line("## Summary"), "other heading untouched");
	should::is_equal("plain", agent_session::escape_body_line("plain"), "plain text untouched");

	std::vector<std::string> lines;
	agent_session::append_entry(lines, agent_entry_kind::agent, {}, "## You\nnot a new entry");

	const auto entries = agent_session::parse(lines);
	should::is_equal(size_t{1}, entries.size(), "escaped body stays in one entry");
}

static void should_session_read_options()
{
	const auto lines = agent_session::to_lines(sample_session);
	const auto options = agent_session::read_options(lines, agent_session::parse(lines));

	should::is_equal("claude-sonnet-4.5", options.model, "model");
	should::is_equal(false, options.yolo, "yolo off");

	const auto on = agent_session::to_lines("## Session\n- yolo: on\n");
	should::is_equal_true(agent_session::read_options(on, agent_session::parse(on)).yolo, "yolo on");

	// Missing options fall back rather than failing
	const auto empty = agent_session::to_lines("## You\nhi\n");
	const auto fallback = agent_session::read_options(empty, agent_session::parse(empty));
	should::is_equal("", fallback.model, "no model");
	should::is_equal(false, fallback.yolo, "no yolo");
}

static void should_session_set_options_preserving_unknown_keys()
{
	auto lines = agent_session::to_lines(sample_session);
	agent_session::set_option(lines, "yolo", "on");

	auto options = agent_session::read_options(lines, agent_session::parse(lines));
	should::is_equal_true(options.yolo, "yolo updated");
	should::is_equal("claude-sonnet-4.5", options.model, "model untouched");
	should::is_equal_true(agent_session::to_text(lines).find("Fix the wrap cache") != std::string::npos,
	                      "conversation untouched");

	// A key we do not know about survives a write
	auto custom = agent_session::to_lines("## Session\n- unknown-key: keep me\n- model: gpt-5\n");
	agent_session::set_option(custom, "model", "gpt-6");
	const auto text = agent_session::to_text(custom);

	should::is_equal_true(text.find("- unknown-key: keep me") != std::string::npos, "unknown key kept");
	should::is_equal_true(text.find("- model: gpt-6") != std::string::npos, "known key updated");
	should::is_equal_true(text.find("gpt-5") == std::string::npos, "old value gone");

	// A missing key is added to the existing block
	auto without = agent_session::to_lines("## Session\n- model: gpt-5\n\n## You\nhi\n");
	agent_session::set_option(without, "yolo", "on");
	should::is_equal_true(agent_session::read_options(without, agent_session::parse(without)).yolo, "key added");
	should::is_equal_true(agent_session::to_text(without).find("## You\nhi") != std::string::npos,
	                      "added inside the session block");

	// An empty file grows a header
	std::vector<std::string> fresh;
	agent_session::set_option(fresh, "model", "gpt-5");
	should::is_equal_true(agent_session::to_text(fresh).starts_with(agent_session::file_header), "header written");
	should::is_equal("gpt-5", agent_session::read_options(fresh, agent_session::parse(fresh)).model, "model set");
}

static void should_session_choose_option()
{
	auto lines = agent_session::to_lines(sample_session);
	auto entries = agent_session::parse(lines);

	agent_session::choose_option(lines, entries.back(), 1);
	entries = agent_session::parse(lines);

	should::is_equal(1, entries.back().chosen_option(), "second option chosen");
	should::is_equal(false, entries.back().options[0].chosen, "first cleared");
	should::is_equal("src/document.cpp", entries.back().options[1].label, "label preserved");

	// Out of range does nothing
	const auto before = agent_session::to_text(lines);
	agent_session::choose_option(lines, entries.back(), 99);
	should::is_equal(before, agent_session::to_text(lines), "out of range ignored");
}

static void should_session_append_entries_and_chunks()
{
	std::vector<std::string> lines;
	agent_session::ensure_header(lines);
	agent_session::append_entry(lines, agent_entry_kind::user, {}, "hello there");
	agent_session::append_entry(lines, agent_entry_kind::agent, {}, {});

	agent_session::append_chunk(lines, "Hel");
	agent_session::append_chunk(lines, "lo");
	agent_session::append_chunk(lines, " world");

	const auto entries = agent_session::parse(lines);
	should::is_equal(size_t{3}, entries.size(), "session, user, agent");
	should::is_equal_true(entries[2].kind == agent_entry_kind::agent, "last is the agent");
	should::is_equal_true(agent_session::to_text(lines).find("Hello world") != std::string::npos,
	                      "chunks joined into one line");

	// A tool call carries its title into the heading and back out again
	agent_session::append_entry(lines, agent_entry_kind::tool_call, "git status (approved)", {});
	const auto with_tool = agent_session::parse(lines);
	should::is_equal_true(with_tool.back().kind == agent_entry_kind::tool_call, "tool entry");
	should::is_equal("git status", with_tool.back().title, "tool title");
	should::is_equal("approved", with_tool.back().status, "tool status");

	// Exactly one blank line separates entries however often we append
	should::is_equal_true(agent_session::to_text(lines).find("\n\n\n") == std::string::npos, "no double blanks");
}

static void should_session_survive_hand_mangled_files()
{
	constexpr std::string_view mangled =
		"## Session\n"
		"- yolo\n"                       // no colon
		"-[x] not an option\n"           // missing space
		"## You\n"
		"### Tool:\n"                    // no title
		"### Question:\n"
		"- [y] bad mark\n"
		"- [ ]\n"                        // empty label
		"##NoSpace\n"
		"   ## Indented\n";

	const auto lines = agent_session::to_lines(mangled);
	const auto entries = agent_session::parse(lines);

	should::is_equal(mangled, agent_session::to_text(lines), "still round trips");
	should::is_equal_true(entries.size() >= 2, "still finds the real headings");
	should::is_equal_true(entries[0].kind == agent_entry_kind::session, "session found");

	const auto options = agent_session::read_options(lines, entries);
	should::is_equal(false, options.yolo, "malformed bullet ignored");
	should::is_equal("", options.model, "no model");
}

static void should_parse_slash_commands()
{
	using agent_session::parse_command;

	should::is_equal_true(parse_command("hello there").command == agent_command::prompt, "plain text");
	should::is_equal("hello there", parse_command("hello there").text, "prompt text");
	should::is_equal_true(parse_command("what about a/b?").command == agent_command::prompt,
	                      "slash inside text is not a command");

	should::is_equal_true(parse_command("/help").command == agent_command::help, "help");
	should::is_equal_true(parse_command("/h").command == agent_command::help, "help alias");
	should::is_equal_true(parse_command("/clear").command == agent_command::clear, "clear");
	should::is_equal_true(parse_command("/c").command == agent_command::clear, "clear alias");
	should::is_equal_true(parse_command("/stop").command == agent_command::stop, "stop");
	should::is_equal_true(parse_command("/s").command == agent_command::stop, "stop alias");
	should::is_equal_true(parse_command("/models").command == agent_command::models, "models");
	should::is_equal_true(parse_command("/m").command == agent_command::models, "models alias");
	should::is_equal_true(parse_command("/yolo").command == agent_command::yolo, "yolo");

	should::is_equal_true(parse_command("  /help  ").command == agent_command::help, "surrounding space");
	should::is_equal("gpt-5", parse_command("/models gpt-5").text, "argument captured");

	// Unknown unless the agent advertised it
	should::is_equal_true(parse_command("/context").command == agent_command::unknown, "unknown by default");
	should::is_equal("context", parse_command("/context").name, "unknown keeps the name");

	const advertised_command advertised[] = {{"context", "Show token usage"}};
	const auto forwarded = parse_command("/context detail", advertised);
	should::is_equal_true(forwarded.command == agent_command::forward, "forwarded when advertised");
	should::is_equal("/context detail", forwarded.text, "forwarded verbatim");
}

// The table is the single source of truth, so help can never drift from the parser
static void should_generate_help_from_the_command_table()
{
	const auto text = agent_session::help_text();

	for (const auto& def : agent_session::command_table())
	{
		should::is_equal_true(text.find(std::format("/{}", def.name)) != std::string::npos,
		                      std::format("help lists /{}", def.name));
		should::is_equal_true(text.find(def.description) != std::string::npos,
		                      std::format("help describes /{}", def.name));

		if (!def.alias.empty())
			should::is_equal_true(text.find(std::format("/{}", def.alias)) != std::string::npos,
			                      std::format("help lists the /{} alias", def.alias));

		// Every listed command must actually parse
		should::is_equal_true(agent_session::parse_command(std::format("/{}", def.name)).command == def.command,
		                      "help entry parses");
	}

	const advertised_command advertised[] = {{"context", "Show token usage"}};
	const auto with_agent = agent_session::help_text(advertised);
	should::is_equal_true(with_agent.find("/context") != std::string::npos, "lists agent commands");
	should::is_equal_true(with_agent.find("Show token usage") != std::string::npos, "describes them");
}

static void should_read_advertised_commands()
{
	const auto parsed = json::parse(
		R"({"availableCommands":[{"name":"context","description":"Show usage"},{"name":"","description":"skip"},{"name":"plan"}]})");

	const auto commands = agent_session::read_advertised_commands(parsed.root);

	should::is_equal(size_t{2}, commands.size(), "nameless entry skipped");
	should::is_equal("context", commands[0].name, "first name");
	should::is_equal("Show usage", commands[0].description, "first description");
	should::is_equal("plan", commands[1].name, "second name");
	should::is_equal("", commands[1].description, "missing description is empty");
}

static json::value make_update(const std::string_view text)
{
	return json::parse(text).root;
}

// A token stream must extend one line rather than growing the file
static void should_stream_chunks_into_one_line()
{
	std::vector<std::string> lines;
	agent_stream_state state;

	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Hel"}})"));

	const auto after_first = lines.size();

	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"lo "}})"));
	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"world"}})"));

	should::is_equal(after_first, lines.size(), "later chunks add no lines");

	const auto entries = agent_session::parse(lines);
	should::is_equal(size_t{1}, entries.size(), "one entry");
	should::is_equal_true(entries[0].kind == agent_entry_kind::agent, "agent entry");
	should::is_equal_true(agent_session::to_text(lines).find("Hello world") != std::string::npos, "joined");

	// A thought opens its own entry, and a following message opens a fresh one
	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"agent_thought_chunk","content":{"type":"text","text":"pondering"}})"));
	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"done"}})"));

	const auto kinds = agent_session::parse(lines);
	should::is_equal(size_t{3}, kinds.size(), "three entries");
	should::is_equal_true(kinds[1].kind == agent_entry_kind::thought, "thought entry");
	should::is_equal_true(kinds[2].kind == agent_entry_kind::agent, "new agent entry");
}

static void should_track_tool_call_lifecycle()
{
	std::vector<std::string> lines;
	agent_stream_state state;

	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"tool_call","toolCallId":"t1","title":"git status","kind":"execute","status":"pending"})"));

	auto entries = agent_session::parse(lines);
	should::is_equal_true(entries.back().kind == agent_entry_kind::tool_call, "tool entry");
	should::is_equal("git status", entries.back().title, "title");
	should::is_equal("pending", entries.back().status, "pending");

	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"tool_call_update","toolCallId":"t1","status":"completed"})"));

	entries = agent_session::parse(lines);
	should::is_equal(size_t{1}, entries.size(), "updated in place, not appended");
	should::is_equal("git status", entries.back().title, "title kept");
	should::is_equal("completed", entries.back().status, "status updated");

	// An update for a call we never saw changes nothing
	const auto before = agent_session::to_text(lines);
	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"tool_call_update","toolCallId":"nope","status":"failed"})"));
	should::is_equal(before, agent_session::to_text(lines), "unknown tool call ignored");

	// A title-less call falls back to its kind
	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"tool_call","toolCallId":"t2","kind":"read","status":"pending"})"));
	should::is_equal("read", agent_session::parse(lines).back().title, "kind used as the title");
}

static void should_write_plans_as_options()
{
	std::vector<std::string> lines;
	agent_stream_state state;

	agent_session::apply_update(lines, state, make_update(
		                            R"({"sessionUpdate":"plan","entries":[{"content":"Read the file","status":"completed"},{"content":"Fix the bug","status":"pending"}]})"));

	const auto entries = agent_session::parse(lines);
	should::is_equal_true(entries.back().kind == agent_entry_kind::plan, "plan entry");
	should::is_equal(size_t{2}, entries.back().options.size(), "two steps");
	should::is_equal_true(entries.back().options[0].chosen, "completed step ticked");
	should::is_equal(false, entries.back().options[1].chosen, "pending step not ticked");
	should::is_equal("Fix the bug", entries.back().options[1].label, "step text");
}

static void should_ignore_unknown_updates()
{
	std::vector<std::string> lines;
	agent_stream_state state;

	agent_session::apply_update(lines, state, make_update(R"({"sessionUpdate":"something_new","data":1})"));
	agent_session::apply_update(lines, state, make_update(R"({})"));

	should::is_equal(size_t{0}, lines.size(), "nothing written");
}

// The document pane must keep at least a usable width however the splitters are dragged
static void should_clamp_agent_splitter_to_the_document_pane()
{
	const auto state = create_test_app();
	const pf::irect bounds{0, 0, 1000, 800};

	state->_panel_splitter._ratio = 0.2;
	auto agent_area = state->agent_splitter_bounds(bounds);
	should::is_equal_true(agent_area.left >= state->_panel_splitter.split_pos(bounds), "starts after the list");

	// Dragging the list splitter far right collapses the agent area rather than pushing it off screen
	state->_panel_splitter._ratio = splitter::max_ratio;
	agent_area = state->agent_splitter_bounds(bounds);
	should::is_equal_true(agent_area.left > state->_panel_splitter.split_pos(bounds), "never crosses");
	should::is_equal_true(agent_area.right >= agent_area.left, "never inverts");
	should::is_equal_true(agent_area.right <= bounds.right, "stays inside the window");

	// Dragging the agent splitter to either extreme still leaves both panes on screen
	for (const auto ratio : {splitter::min_ratio, 0.5, splitter::max_ratio})
	{
		state->_agent_splitter._ratio = ratio;
		const auto split = state->_agent_splitter.split_pos(state->agent_splitter_bounds(bounds));
		should::is_equal_true(split > state->_panel_splitter.split_pos(bounds), "document pane survives");
		should::is_equal_true(split <= bounds.right, "agent pane stays on screen");
	}
}

static void should_grow_agent_input_to_six_rows()
{
	const auto state = create_test_app();
	agent_view view(*state);

	should::is_equal(1, view.input_rows(), "empty is one row");

	view.set_input_text("one line");
	should::is_equal(1, view.input_rows(), "single line");

	view.set_input_text("one\ntwo\nthree");
	should::is_equal(3, view.input_rows(), "three lines");

	view.set_input_text("1\n2\n3\n4\n5\n6");
	should::is_equal(agent_view::max_input_rows, view.input_rows(), "six lines");

	// Beyond the cap the box stops growing and the text scrolls instead
	view.set_input_text("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
	should::is_equal(agent_view::max_input_rows, view.input_rows(), "capped at six");
}

// Editing the transcript must not disturb the document pane's caches
static void should_keep_the_session_document_separate_from_the_editor()
{
	const auto state = create_test_app();
	const auto editor_doc = state->doc();

	const auto session = state->session_item();
	should::is_equal_true(session != nullptr, "session item exists");
	should::is_equal_true(session->doc != editor_doc, "a document of its own");
	should::is_equal(std::string(agent_session::file_name), session->name, "named session.md");

	const auto editor_lines = static_cast<int>(editor_doc->size());
	state->on_agent_input("hello agent");

	should::is_equal(editor_lines, static_cast<int>(editor_doc->size()), "editor untouched");
	should::is_equal_true(static_cast<int>(session->doc->size()) > editor_lines, "transcript grew");
	should::is_equal_true(state->doc() == editor_doc, "active document unchanged");
}

static void should_handle_agent_slash_commands_in_the_panel()
{
	const auto state = create_test_app();
	state->ensure_agent_host();

	// Tests must never launch the real agent
	state->_agent_host->locate = [] { return pf::file_path{}; };

	state->on_agent_input("/help");
	auto text = state->session_item()->doc->str();
	should::is_equal_true(text.find("/clear") != std::string::npos, "help lists the commands");

	state->on_agent_input("/nonsense");
	text = state->session_item()->doc->str();
	should::is_equal_true(text.find("Unknown command /nonsense") != std::string::npos, "unknown reported");

	// A plain message is recorded as the user's turn, then reports that no agent was found
	state->on_agent_input("do the thing");
	text = state->session_item()->doc->str();
	should::is_equal_true(text.find("do the thing") != std::string::npos, "user turn recorded");
	should::is_equal_true(text.find("Could not find") != std::string::npos, "missing agent reported");

	const auto entries = agent_session::parse(agent_session::to_lines(text));
	const auto user_entries = std::ranges::count_if(entries, [](const agent_entry& e)
	{
		return e.kind == agent_entry_kind::user;
	});
	should::is_equal_true(user_entries >= 1, "user entry present");

	// Clearing starts a fresh transcript
	state->on_agent_input("/clear");
	text = state->session_item()->doc->str();
	should::is_equal_true(text.find("do the thing") == std::string::npos, "history cleared");
	should::is_equal_true(text.starts_with(agent_session::file_header), "header restored");
}

// collecting_sink — Records what the host writes, standing in for the pane
struct collecting_sink final : agent_host::events
{
	std::vector<std::string> lines;
	std::string status;

	void transcript_changed(const int first, const std::span<const std::string> replacement) override
	{
		lines.resize(static_cast<size_t>(std::clamp(first, 0, static_cast<int>(lines.size()))));

		for (const auto& line : replacement)
			lines.push_back(line);
	}

	void agent_status_changed(const std::string_view text) override { status = text; }

	[[nodiscard]] std::string text() const { return agent_session::to_text(lines); }
};

static std::unique_ptr<agent_host> connected_host(collecting_sink& sink, recording_transport*& wire_out)
{
	auto host = std::make_unique<agent_host>(sink);
	auto wire = std::make_unique<recording_transport>();
	wire_out = wire.get();

	host->connect(std::move(wire), pf::file_path{"C:\\work"});

	host->on_agent_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"protocolVersion":1}}}})",
	                                wire_out->last_id()));
	host->on_agent_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"sessionId":"s1"}}}})",
	                                wire_out->last_id()));
	return host;
}

static void should_run_an_agent_turn()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	should::is_equal_true(host->connected(), "connected");
	should::is_equal("Ready", sink.status, "ready status");

	host->submit("fix the bug");
	should::is_equal_true(sink.text().find("fix the bug") != std::string::npos, "user turn written");
	should::is_equal_true(host->busy(), "turn in flight");
	should::is_equal_true(sink.status.find("/s to stop") != std::string::npos, "status offers a way out");

	const auto prompt_id = wire->last_id();

	host->on_agent_line(
		R"({"jsonrpc":"2.0","method":"session/update","params":{"update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Look"}}}})");
	host->on_agent_line(
		R"({"jsonrpc":"2.0","method":"session/update","params":{"update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"ing"}}}})");

	should::is_equal_true(sink.text().find("Looking") != std::string::npos, "chunks joined");

	host->on_agent_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"stopReason":"end_turn"}}}})", prompt_id));
	should::is_equal(false, host->busy(), "turn finished");
	should::is_equal("Ready", sink.status, "back to ready");
}

// A streamed token must rewrite one line, not the whole transcript
static void should_patch_only_the_tail_while_streaming()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	host->submit("go");

	auto smallest_first = std::numeric_limits<int>::max();

	struct watching_sink final : agent_host::events
	{
		collecting_sink& inner;
		int& smallest;

		watching_sink(collecting_sink& s, int& f) : inner(s), smallest(f)
		{
		}

		void transcript_changed(const int first, const std::span<const std::string> replacement) override
		{
			smallest = std::min(smallest, static_cast<int>(replacement.size()));
			inner.transcript_changed(first, replacement);
		}

		void agent_status_changed(const std::string_view text) override { inner.agent_status_changed(text); }
	};

	watching_sink watcher(sink, smallest_first);
	auto streaming = std::make_unique<agent_host>(watcher);
	streaming->adopt(sink.lines);

	auto wire2 = std::make_unique<recording_transport>();
	streaming->connect(std::move(wire2), pf::file_path{"C:\\work"});

	for (const auto* chunk : {"a", "b", "c"})
	{
		streaming->on_agent_line(std::format(
			R"({{"jsonrpc":"2.0","method":"session/update","params":{{"update":{{"sessionUpdate":"agent_message_chunk","content":{{"type":"text","text":"{}"}}}}}}}})",
			chunk));
	}

	should::is_equal(1, smallest_first, "a later chunk rewrites a single line");
}

static void should_ask_before_running_a_tool()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	host->submit("check the repo");

	host->on_agent_line(
		R"({"jsonrpc":"2.0","id":77,"method":"session/request_permission","params":{"toolCall":{"title":"git status"},"options":[{"optionId":"yes","name":"Allow","kind":"allow_once"},{"optionId":"no","name":"Reject","kind":"reject_once"}]}})");

	const auto text = sink.text();
	should::is_equal_true(text.find("git status") != std::string::npos, "names the tool");
	should::is_equal_true(text.find("1. Allow") != std::string::npos, "numbers the options");
	should::is_equal_true(text.find("2. Reject") != std::string::npos, "lists every option");
	should::is_equal_true(sink.status.find("Waiting") != std::string::npos, "status asks for an answer");

	// Nothing was sent back until the user chose
	should::is_equal_true(wire->last()["result"].is_null(), "no answer yet");

	host->submit("2");

	const auto reply = wire->last();
	should::is_equal(77, static_cast<int>(reply["id"].integer()), "answers the right request");
	should::is_equal("selected", reply["result"]["outcome"]["outcome"].text(), "selected outcome");
	should::is_equal("no", reply["result"]["outcome"]["optionId"].text(), "the option the user picked");

	// The choice is recorded in the file
	const auto entries = agent_session::parse(sink.lines);
	const auto question = std::ranges::find_if(entries, [](const agent_entry& e)
	{
		return e.kind == agent_entry_kind::question;
	});
	should::is_equal_true(question != entries.end(), "question entry written");
	should::is_equal(1, question->chosen_option(), "second option ticked");
}

static void should_auto_approve_only_in_yolo_mode()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	host->submit("/yolo");
	should::is_equal_true(host->yolo(), "yolo on");
	should::is_equal_true(sink.text().find("yolo: on") != std::string::npos, "recorded in the session block");

	host->on_agent_line(
		R"({"jsonrpc":"2.0","id":88,"method":"session/request_permission","params":{"toolCall":{"title":"rm -rf"},"options":[{"optionId":"deny","name":"Reject","kind":"reject_once"},{"optionId":"ok","name":"Allow","kind":"allow_once"}]}})");

	const auto reply = wire->last();
	should::is_equal(88, static_cast<int>(reply["id"].integer()), "answered immediately");
	should::is_equal("ok", reply["result"]["outcome"]["optionId"].text(), "chose the allow option");
	should::is_equal_true(sink.text().find("Allowed automatically: rm -rf") != std::string::npos,
	                      "still recorded in the transcript");

	// Turning it back off restores the prompt
	host->submit("/yolo");
	should::is_equal(false, host->yolo(), "yolo off");
	should::is_equal_true(sink.text().find("yolo: off") != std::string::npos, "recorded as off");
}

static void should_stop_a_running_turn()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	host->submit("/s");
	should::is_equal_true(sink.text().find("not working on anything") != std::string::npos, "nothing to stop");

	host->submit("long task");
	const auto prompt_id = wire->last_id();
	host->submit("/s");

	should::is_equal("session/cancel", wire->last()["method"].text(), "cancel sent");

	host->on_agent_line(std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"stopReason":"cancelled"}}}})", prompt_id));
	should::is_equal(false, host->busy(), "turn ended");
	should::is_equal_true(sink.text().find("cancelled") != std::string::npos, "recorded in the transcript");
}

static void should_report_a_lost_agent()
{
	collecting_sink sink;
	recording_transport* wire = nullptr;
	const auto host = connected_host(sink, wire);

	host->submit("work");
	host->on_agent_exit(1);

	should::is_equal(false, host->connected(), "no longer connected");
	should::is_equal(false, host->busy(), "no turn in flight");
	should::is_equal("Not connected", sink.status, "status reports it");
	should::is_equal_true(sink.text().find("the agent stopped") != std::string::npos, "written to the transcript");
}

tests::run_result run_all_tests_result(){
	tests tests;

	// Document tests
	tests.register_test("should insert chars", should_insert_single_chars);
	tests.register_test("should split line", should_split_line);
	tests.register_test("should combine line", should_combine_line);
	tests.register_test("should delete chars", should_delete_chars);
	tests.register_test("should delete selection", should_delete_selection);
	tests.register_test("should delete 1 line selection", should_delete1_line_selection);
	tests.register_test("should delete 2 line selection", should_delete2_line_selection);
	tests.register_test("should insert selection", should_insert_selection);
	tests.register_test("should insert crlf text", should_insert_crlf_text);
	tests.register_test("should return selection", should_return_selection);
	tests.register_test("should cut and paste", should_cut_and_paste);
	tests.register_test("should reformat json preserves strings", should_reformat_json_preserves_strings);
	tests.register_test("should ignore carriage return char", should_ignore_carriage_return_char);
	tests.register_test("should delete back over multi-byte char", should_delete_back_multibyte_char);
	tests.register_test("should grow max_line_length while typing", should_max_line_length_grows_on_typing);

	// Calculator tests
	tests.register_test("should calc expressions", should_calc_expressions);
	tests.register_test("should calc reject bad input", should_calc_rejects_bad_input);

	// JSON tests
	tests.register_test("should json round trip", should_json_round_trip);
	tests.register_test("should json read members", should_json_read_members);
	tests.register_test("should json accessors fall back", should_json_accessors_fall_back);
	tests.register_test("should json parse escapes", should_json_parse_escapes);
	tests.register_test("should json replace lone surrogates", should_json_replace_lone_surrogates);
	tests.register_test("should json escape control characters", should_json_escape_control_characters);
	tests.register_test("should json keep integers exact", should_json_keep_integers_exact);
	tests.register_test("should json reject bad numbers", should_json_reject_bad_numbers);
	tests.register_test("should json reject malformed input", should_json_reject_malformed_input);
	tests.register_test("should json limit nesting depth", should_json_limit_nesting_depth);
	tests.register_test("should json build messages", should_json_build_messages);
	tests.register_test("should json replace duplicate keys", should_json_replace_duplicate_keys);
	tests.register_test("should json pass through utf8", should_json_pass_through_utf8);

	// Child process plumbing tests
	tests.register_test("should split lines across read boundaries", should_split_lines_across_read_boundaries);
	tests.register_test("should split lines handle endings and blanks",
	                    should_split_lines_handle_endings_and_blanks);
	tests.register_test("should split lines discard oversized records",
	                    should_split_lines_discard_oversized_records);
	tests.register_test("should quote command arguments", should_quote_command_arguments);
	tests.register_test("should detect shell metacharacters", should_detect_shell_metacharacters);

	// ACP protocol tests
	tests.register_test("should acp complete handshake", should_acp_complete_handshake);
	tests.register_test("should acp report handshake failure", should_acp_report_handshake_failure);
	tests.register_test("should acp send prompt and end turn", should_acp_send_prompt_and_end_turn);
	tests.register_test("should acp refuse prompt before ready", should_acp_refuse_prompt_before_ready);
	tests.register_test("should acp cancel turn", should_acp_cancel_turn);
	tests.register_test("should acp dispatch session updates", should_acp_dispatch_session_updates);
	tests.register_test("should acp answer permission requests", should_acp_answer_permission_requests);
	tests.register_test("should acp reject unknown requests", should_acp_reject_unknown_requests);
	tests.register_test("should acp survive malformed input", should_acp_survive_malformed_input);
	tests.register_test("should acp fail pending requests on disconnect",
	                    should_acp_fail_pending_requests_on_disconnect);
	tests.register_test("should acp parse stop reasons", should_acp_parse_stop_reasons);

	// session.md tests
	tests.register_test("should session round trip exactly", should_session_round_trip_exactly);
	tests.register_test("should session parse entries", should_session_parse_entries);
	tests.register_test("should session keep agent markdown inside its entry",
	                    should_session_keep_agent_markdown_inside_its_entry);
	tests.register_test("should session escape heading like body", should_session_escape_heading_like_body);
	tests.register_test("should session read options", should_session_read_options);
	tests.register_test("should session set options preserving unknown keys",
	                    should_session_set_options_preserving_unknown_keys);
	tests.register_test("should session choose option", should_session_choose_option);
	tests.register_test("should session append entries and chunks", should_session_append_entries_and_chunks);
	tests.register_test("should session survive hand mangled files", should_session_survive_hand_mangled_files);
	tests.register_test("should parse slash commands", should_parse_slash_commands);
	tests.register_test("should generate help from the command table",
	                    should_generate_help_from_the_command_table);
	tests.register_test("should read advertised commands", should_read_advertised_commands);
	tests.register_test("should stream chunks into one line", should_stream_chunks_into_one_line);
	tests.register_test("should track tool call lifecycle", should_track_tool_call_lifecycle);
	tests.register_test("should write plans as options", should_write_plans_as_options);
	tests.register_test("should ignore unknown updates", should_ignore_unknown_updates);

	// Agent panel tests
	tests.register_test("should clamp agent splitter to the document pane",
	                    should_clamp_agent_splitter_to_the_document_pane);
	tests.register_test("should grow agent input to six rows", should_grow_agent_input_to_six_rows);
	tests.register_test("should keep the session document separate from the editor",
	                    should_keep_the_session_document_separate_from_the_editor);
	tests.register_test("should handle agent slash commands in the panel",
	                    should_handle_agent_slash_commands_in_the_panel);
	tests.register_test("should run an agent turn", should_run_an_agent_turn);
	tests.register_test("should patch only the tail while streaming",
	                    should_patch_only_the_tail_while_streaming);
	tests.register_test("should ask before running a tool", should_ask_before_running_a_tool);
	tests.register_test("should auto approve only in yolo mode", should_auto_approve_only_in_yolo_mode);
	tests.register_test("should stop a running turn", should_stop_a_running_turn);
	tests.register_test("should report a lost agent", should_report_a_lost_agent);

	// String utility tests
	tests.register_test("should to_lower", should_to_lower);
	tests.register_test("should unquote", should_unquote);
	tests.register_test("should icmp", should_icmp);
	tests.register_test("should find_in_text", should_find_in_text);
	tests.register_test("should combine lines", should_combine_lines);
	tests.register_test("should replace string", should_replace_string);
	tests.register_test("should is_empty", should_is_empty);

	// Geometry tests
	tests.register_test("should pf::ipoint ops", should_ipoint_ops);
	tests.register_test("should pf::isize ops", should_isize_ops);
	tests.register_test("should pf::irect ops", should_irect_ops);

	// Encoding tests
	tests.register_test("should save preserve BOM presence", should_save_preserves_bom_presence);
	tests.register_test("should save preserve UTF-16 encoding", should_save_preserves_utf16_encoding);

	// Misc utility tests
	tests.register_test("should clamp value", should_clamp_value);
	tests.register_test("should fnv1a hash", should_fnv1a_hash);
	tests.register_test("should pf::file_path ops", should_file_path_ops);

	// Encoding detection tests (BOM)
	tests.register_test("should detect UTF-8 BOM", should_detect_utf8_bom);
	tests.register_test("should detect UTF-16 LE BOM", should_detect_utf16le_bom);
	tests.register_test("should detect UTF-16 BE BOM", should_detect_utf16be_bom);
	tests.register_test("should detect UTF-32 LE BOM", should_detect_utf32le_bom);
	tests.register_test("should detect UTF-32 BE BOM", should_detect_utf32be_bom);
	tests.register_test("should detect UTF-16 LE no BOM", should_detect_utf16le_no_bom);
	tests.register_test("should detect UTF-16 BE no BOM", should_detect_utf16be_no_bom);
	tests.register_test("should detect UTF-8 default", should_detect_utf8_default);
	tests.register_test("should detect UTF-8 without BOM", should_detect_utf8_without_bom);
	tests.register_test("should detect small files", should_detect_small_files);
	tests.register_test("should prioritize UTF-32 over UTF-16", should_prioritize_utf32_over_utf16_bom);

	// Encoding conversion tests
	tests.register_test("should UTF-8 to UTF-16 ASCII", should_utf8_to_utf16_ascii);
	tests.register_test("should UTF-8 to UTF-16 multibyte", should_utf8_to_utf16_multibyte);
	tests.register_test("should UTF-16 to UTF-8 roundtrip", should_utf16_to_utf8_roundtrip);
	tests.register_test("should UTF-8 to UTF-16 mixed", should_utf8_to_utf16_mixed);
	tests.register_test("should UTF-8 to UTF-16 various symbols", should_utf8_to_utf16_various_symbols);

	// Line ending detection tests
	tests.register_test("should detect CRLF line endings", should_detect_crlf_line_endings);
	tests.register_test("should detect LF line endings", should_detect_lf_line_endings);
	tests.register_test("should detect LFCR line endings", should_detect_lfcr_line_endings);

	// Markdown tests
	tests.register_test("should md highlight heading", should_md_highlight_heading);
	tests.register_test("should md highlight bold", should_md_highlight_bold);
	tests.register_test("should md highlight italic", should_md_highlight_italic);
	tests.register_test("should md highlight link", should_md_highlight_link);
	tests.register_test("should md highlight list", should_md_highlight_list);
	tests.register_test("should highlight cpp", should_highlight_cpp);
	tests.register_test("should highlight rust", should_highlight_rust);
	tests.register_test("should highlight python", should_highlight_python);
	tests.register_test("should highlight powershell", should_highlight_powershell);
	tests.register_test("should highlight plain text", should_highlight_plain_text);
	tests.register_test("should treat utf8 bytes as word bytes", should_treat_utf8_bytes_as_word_bytes);
	tests.register_test("should find text ignoring case for non ascii",
	                    should_find_text_ignoring_case_for_non_ascii);
	tests.register_test("should apply gitignore rules", should_apply_gitignore_rules);
	tests.register_test("should apply nested gitignore relative to its folder",
	                    should_apply_nested_gitignore_relative_to_its_folder);
	tests.register_test("should match gitignore double star", should_match_gitignore_double_star);
	tests.register_test("should narrow search to previous matches", should_narrow_search_to_previous_matches);
	tests.register_test("should refuse to save a truncated document", should_refuse_to_save_truncated_document);
	tests.register_test("should evict unused documents", should_evict_unused_documents);
	tests.register_test("should adjust drop target for removed selection",
	                    should_adjust_drop_target_for_removed_selection);

	// App state tests
	tests.register_test("should app_state new_doc is markdown", should_app_state_new_doc_is_markdown);
	tests.register_test("should app_state is_markdown_path", should_app_state_is_markdown_path);
	tests.register_test("should cap search results", should_cap_search_results);
	tests.register_test("should count search results when group collapsed",
	                    should_count_search_results_when_group_collapsed);
	tests.register_test("should create_new_file with content", should_create_new_file_with_content);
	tests.register_test("should create_new_file added to tree", should_create_new_file_added_to_tree);
	tests.register_test("should create_new_file with unique name", should_create_new_file_with_unique_name);
	tests.register_test("should restore per-document view mode", should_restore_per_document_view_mode);
	tests.register_test("should create multiple new files", should_create_multiple_new_files);
	tests.register_test("should create_new_file sorted in tree", should_create_new_file_sorted_in_tree);
	tests.register_test("should create_new_folder with unique name", should_create_new_folder_with_unique_name);
	tests.register_test("should refresh_index preserve unsaved doc folder",
	                    should_refresh_index_preserve_unsaved_doc_folder);
	tests.register_test("should remember recent root folders most recent first",
	                    should_remember_recent_root_folders_most_recent_first);
	tests.register_test("should cap recent root folders at eight",
	                    should_cap_recent_root_folders_at_eight);
	tests.register_test("should restore recent root folders in saved order",
	                    should_restore_recent_root_folders_in_saved_order);
	tests.register_test("should keep max line length after editing a short line",
	                    should_keep_max_line_length_after_editing_a_short_line);
	tests.register_test("should select word containing non ascii",
	                    should_select_word_containing_non_ascii);
	tests.register_test("should insert non ascii codepoint as utf8",
	                    should_insert_non_ascii_codepoint_as_utf8);
	tests.register_test("should block edits to read only document",
	                    should_block_edits_to_read_only_document);
	tests.register_test("should restore last open file when switching recent root folder",
	                    should_restore_last_open_file_when_switching_recent_root_folder);
	tests.register_test("should select search match after deferred load",
	                    should_select_search_match_after_deferred_load);
	tests.register_test("should scroll to search match after deferred load",
	                    should_scroll_to_search_match_after_deferred_load);
	tests.register_test("should doc is_json", should_doc_is_json);
	tests.register_test("should doc sort_remove_duplicates", should_doc_sort_remove_duplicates);
	tests.register_test("should doc sort_remove_duplicates keeps case", should_doc_sort_remove_duplicates_keeps_case);
	tests.register_test("should doc reformat_json", should_doc_reformat_json);
	tests.register_test("should undo back to clean", should_undo_back_to_clean);
	tests.register_test("should undo delete back to clean", should_undo_delete_back_to_clean);
	tests.register_test("should undo multiple to clean", should_undo_multiple_to_clean);

	// Search tests
	tests.register_test("should search doc basic", should_search_doc_basic);
	tests.register_test("should search doc match positions", should_search_doc_match_positions);
	tests.register_test("should search doc case insensitive", should_search_doc_case_insensitive);
	tests.register_test("should search doc empty clears", should_search_doc_empty_clears);
	tests.register_test("should search multiple files", should_search_multiple_files);
	tests.register_test("should search no match", should_search_no_match);
	tests.register_test("should clip context of a very long result line",
	                    should_clip_context_of_a_very_long_result_line);

	// Word wrap tests
	tests.register_test("should wrap long lines onto several rows", should_wrap_long_lines_onto_several_rows);
	tests.register_test("should splice wrap when a line is split", should_splice_wrap_when_a_line_is_split);
	tests.register_test("should splice wrap when lines are joined", should_splice_wrap_when_lines_are_joined);
	tests.register_test("should splice wrap when a block is deleted", should_splice_wrap_when_a_block_is_deleted);
	tests.register_test("should splice wrap when a block is inserted", should_splice_wrap_when_a_block_is_inserted);
	tests.register_test("should splice wrap when an edit is undone", should_splice_wrap_when_an_edit_is_undone);
	tests.register_test("should rewrap a dirty line that a later split moved",
	                    should_rewrap_a_dirty_line_that_a_later_split_moved);

	auto result = tests.run_all_result();
	result.output = "# Test results\n\n" + result.output;
	return result;
}

std::string run_all_tests()
{
	return run_all_tests_result().output;
}
