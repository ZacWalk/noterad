// calc.h — Calculator expression parser for evaluating arithmetic expressions

#pragma once

class calc_parser
{
public:
	explicit calc_parser(const std::string_view expression) : _text(expression)
	{
	}

	std::optional<double> parse()
	{
		_pos = 0;
		_error.clear();
		auto value = parse_expression();
		skip_ws();
		if (!_error.empty())
			return std::nullopt;
		if (_pos != _text.size())
		{
			_error = "Unexpected trailing input.";
			return std::nullopt;
		}
		return value;
	}

	std::string error() const { return _error; }

private:
	std::string_view _text;
	size_t _pos = 0;
	int _depth = 0;
	static constexpr int max_depth = 256;
	std::string _error;

	void skip_ws()
	{
		while (_pos < _text.size() && (_text[_pos] == u8' ' || _text[_pos] == u8'\t'))
			++_pos;
	}

	bool consume(const char ch)
	{
		skip_ws();
		if (_pos < _text.size() && _text[_pos] == ch)
		{
			++_pos;
			return true;
		}
		return false;
	}

	double parse_expression()
	{
		double value = parse_term();
		while (_error.empty())
		{
			if (consume(u8'+')) value += parse_term();
			else if (consume(u8'-')) value -= parse_term();
			else break;
		}
		return value;
	}

	double parse_term()
	{
		double value = parse_factor();
		while (_error.empty())
		{
			if (consume(u8'*')) value *= parse_factor();
			else if (consume(u8'/'))
			{
				const auto rhs = parse_factor();
				if (rhs == 0.0)
				{
					_error = "Division by zero.";
					return 0.0;
				}
				value /= rhs;
			}
			else break;
		}
		return value;
	}

	double parse_factor()
	{
		if (++_depth > max_depth)
		{
			_error = "Expression too deeply nested.";
			return 0.0;
		}
		skip_ws();
		double result;
		if (consume(u8'+'))
			result = parse_factor();
		else if (consume(u8'-'))
			result = -parse_factor();
		else if (consume(u8'('))
		{
			result = parse_expression();
			if (!consume(u8')'))
				_error = "Missing closing ')'.";
		}
		else
			result = parse_number();
		--_depth;
		return result;
	}

	double parse_number()
	{
		skip_ws();
		const auto start = _pos;
		bool seen_digit = false;
		bool seen_dot = false;
		while (_pos < _text.size())
		{
			const auto ch = _text[_pos];
			if (ch >= u8'0' && ch <= u8'9')
			{
				seen_digit = true;
				++_pos;
				continue;
			}
			if (ch == u8'.' && !seen_dot)
			{
				seen_dot = true;
				++_pos;
				continue;
			}
			break;
		}

		if (!seen_digit)
		{
			_error = "Expected a number.";
			return 0.0;
		}

		const auto raw = _text.substr(start, _pos - start);
		double value = 0.0;
		const auto* const raw_end = raw.data() + raw.size();
		const auto [ptr, ec] = std::from_chars(raw.data(), raw_end, value);

		if (ec != std::errc{} || ptr != raw_end)
		{
			_error = "Invalid number.";
			return 0.0;
		}

		return value;
	}
};
