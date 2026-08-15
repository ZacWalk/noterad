// json.h — Minimal JSON DOM: parse, build and serialise. UTF-8 in, UTF-8 out.

#pragma once

namespace json
{
	enum class kind : uint8_t { null, boolean, number, string, array, object };

	class value;

	struct member;

	class value
	{
	public:
		value() = default;

		value(std::nullptr_t)
		{
		}

		value(const bool b) : _kind(kind::boolean), _boolean(b)
		{
		}

		value(const int32_t n) : value(static_cast<int64_t>(n))
		{
		}

		value(const uint32_t n) : value(static_cast<int64_t>(n))
		{
		}

		value(const int64_t n) : _kind(kind::number), _integral(true), _int(n),
		                         _number(static_cast<double>(n))
		{
		}

		value(const double d) : _kind(kind::number), _number(d)
		{
		}

		value(std::string s) : _kind(kind::string), _string(std::move(s))
		{
		}

		value(const std::string_view s) : _kind(kind::string), _string(s)
		{
		}

		value(const char* s) : _kind(kind::string), _string(s ? s : "")
		{
		}

		[[nodiscard]] kind type() const { return _kind; }
		[[nodiscard]] bool is(const kind k) const { return _kind == k; }
		[[nodiscard]] bool is_null() const { return _kind == kind::null; }
		[[nodiscard]] bool is_number() const { return _kind == kind::number; }
		[[nodiscard]] bool is_string() const { return _kind == kind::string; }
		[[nodiscard]] bool is_array() const { return _kind == kind::array; }
		[[nodiscard]] bool is_object() const { return _kind == kind::object; }

		// Accessors are total: a type mismatch yields the default, never an error
		[[nodiscard]] std::string_view text(std::string_view fallback = {}) const
		{
			return _kind == kind::string ? std::string_view(_string) : fallback;
		}

		[[nodiscard]] double number(const double fallback = 0.0) const
		{
			return _kind == kind::number ? _number : fallback;
		}

		[[nodiscard]] int64_t integer(const int64_t fallback = 0) const;

		[[nodiscard]] bool boolean(const bool fallback = false) const
		{
			return _kind == kind::boolean ? _boolean : fallback;
		}

		[[nodiscard]] const value& operator[](std::string_view key) const;
		[[nodiscard]] const value& operator[](size_t index) const;
		[[nodiscard]] bool contains(std::string_view key) const;
		[[nodiscard]] size_t size() const;

		[[nodiscard]] const std::vector<value>& items() const { return _items; }
		[[nodiscard]] const std::vector<member>& members() const { return _members; }

		// Building. set() replaces an existing key so a built object never holds duplicates.
		value& set(std::string key, value v);
		value& add(value v);

		[[nodiscard]] std::string to_string() const;
		void write(std::string& out) const;

		static const value& null_value();

	private:
		kind _kind = kind::null;
		bool _boolean = false;
		bool _integral = false;
		int64_t _int = 0;
		double _number = 0.0;
		std::string _string;
		std::vector<value> _items;
		std::vector<member> _members;

		friend value object();
		friend value array();
	};

	struct member
	{
		std::string key;
		value val;
	};

	[[nodiscard]] value object();
	[[nodiscard]] value array();

	// Nesting deeper than this is rejected, so hostile input cannot exhaust the stack
	constexpr int max_parse_depth = 100;

	struct parse_result
	{
		value root;
		bool ok = false;
		std::string error;
		size_t error_offset = 0;

		explicit operator bool() const { return ok; }
	};

	[[nodiscard]] parse_result parse(std::string_view text);

	// Escapes control characters, so the result never contains a raw newline
	void write_quoted(std::string& out, std::string_view s);
}
