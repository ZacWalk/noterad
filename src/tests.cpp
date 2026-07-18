// tests.cpp — Unit tests for document editing, undo/redo, search, and crypto

#include "pch.h"
#include "app.h"
#include "document.h"
#include "app_state.h"
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

	std::string text_from_clipboard() override { return {}; }
	bool text_to_clipboard(std::string_view) override { return false; }
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

static void should_calc_sha256()
{
	const auto text = "hello world";
	const auto output = calc_sha256(text);
	should::is_equal("b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9", to_hex(output));
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

static void should_last_char()
{
	should::is_equal(L'd', last_char("abcd"));
	should::is_equal(0, last_char(""));
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

// ── util.h encoding tests ──────────────────────────────────────────────────────

static void should_hex_roundtrip()
{
	std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
	const auto hex = to_hex(data);
	should::is_equal("deadbeef", hex);

	const auto back = hex_to_data(hex);
	should::is_equal(static_cast<int>(data.size()), static_cast<int>(back.size()));
	for (size_t i = 0; i < data.size(); ++i)
		should::is_equal(data[i], back[i]);
}

static void should_base64_encode()
{
	const std::string text = "Hello";
	const std::vector<uint8_t> data(text.begin(), text.end());
	should::is_equal("SGVsbG8=", to_base64(data));

	std::vector<uint8_t> empty;
	should::is_equal("", to_base64(empty));
}

static void should_aes256_roundtrip()
{
	static const uint8_t key_data[32] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
	};
	uint8_t plaintext[16] = {
		'H', 'e', 'l', 'l', 'o', ' ', 'A', 'E',
		'S', '-', '2', '5', '6', '!', '!', '!'
	};
	uint8_t original[16];
	std::copy_n(plaintext, 16, original);

	aes256 cipher(key_data);
	cipher.encrypt_ecb(plaintext);

	// encrypted data should differ from original
	bool differs = false;
	for (int i = 0; i < 16; ++i)
		if (plaintext[i] != original[i]) differs = true;
	should::is_equal_true(differs);

	aes256 decipher(key_data);
	decipher.decrypt_ecb(plaintext);

	for (int i = 0; i < 16; ++i)
		should::is_equal(original[i], plaintext[i]);
}

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

// ── app_state tests ────────────────────────────────────────────────────────────────────

// sync_scheduler — Test stub that executes tasks immediately on the calling thread.
class sync_scheduler final : public async_scheduler
{
public:
	void run_async(const std::function<void()> task) override { if (task) task(); }
	void run_ui(const std::function<void()> task) override { if (task) task(); }
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
	for (int i = 0; i < app_state::max_search_results + 50; i++)
		many_lines += "x\n";

	std::vector<app_state::search_input> inputs;
	inputs.push_back({pf::file_path{"c:\\folder\\a.txt"}, std::make_shared<document>(null_ev, many_lines)});
	inputs.push_back({pf::file_path{"c:\\folder\\b.txt"}, std::make_shared<document>(null_ev, "x\nx\nx")});

	const auto results = app_state::perform_search(inputs, "x");

	int total = 0;
	for (const auto& entry : results)
		total += static_cast<int>(entry.second.size());

	should::is_equal(app_state::max_search_results, total, "search results capped");
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

tests::run_result run_all_tests_result()
{
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
	tests.register_test("should calc pf::sha256", should_calc_sha256);

	// String utility tests
	tests.register_test("should to_lower", should_to_lower);
	tests.register_test("should unquote", should_unquote);
	tests.register_test("should icmp", should_icmp);
	tests.register_test("should find_in_text", should_find_in_text);
	tests.register_test("should combine lines", should_combine_lines);
	tests.register_test("should replace string", should_replace_string);
	tests.register_test("should last_char", should_last_char);
	tests.register_test("should is_empty", should_is_empty);

	// Geometry tests
	tests.register_test("should pf::ipoint ops", should_ipoint_ops);
	tests.register_test("should pf::isize ops", should_isize_ops);
	tests.register_test("should pf::irect ops", should_irect_ops);

	// Encoding tests
	tests.register_test("should hex roundtrip", should_hex_roundtrip);
	tests.register_test("should base64 encode", should_base64_encode);
	tests.register_test("should pf::aes256 roundtrip", should_aes256_roundtrip);

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

	// App state tests
	tests.register_test("should app_state new_doc is markdown", should_app_state_new_doc_is_markdown);
	tests.register_test("should app_state is_markdown_path", should_app_state_is_markdown_path);
	tests.register_test("should cap search results", should_cap_search_results);
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
	tests.register_test("should restore last open file when switching recent root folder",
	                    should_restore_last_open_file_when_switching_recent_root_folder);
	tests.register_test("should doc is_json", should_doc_is_json);
	tests.register_test("should doc sort_remove_duplicates", should_doc_sort_remove_duplicates);
	tests.register_test("should doc reformat_json", should_doc_reformat_json);
	tests.register_test("should undo back to clean", should_undo_back_to_clean);
	tests.register_test("should undo multiple to clean", should_undo_multiple_to_clean);

	// Search tests
	tests.register_test("should search doc basic", should_search_doc_basic);
	tests.register_test("should search doc match positions", should_search_doc_match_positions);
	tests.register_test("should search doc case insensitive", should_search_doc_case_insensitive);
	tests.register_test("should search doc empty clears", should_search_doc_empty_clears);
	tests.register_test("should search multiple files", should_search_multiple_files);
	tests.register_test("should search no match", should_search_no_match);

	auto result = tests.run_all_result();
	result.output = "# Test results\n\n" + result.output;
	return result;
}

std::string run_all_tests()
{
	return run_all_tests_result().output;
}
