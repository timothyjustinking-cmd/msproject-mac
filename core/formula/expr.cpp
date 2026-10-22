#include "expr.h"

#include <cstdlib>
#include <string>
#include <utility>

#include "fields.h"

namespace core::formula {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }

char lower_char(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(lower_char(c));
    }
    return out;
}

bool equals_ci(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_char(a[i]) != lower_char(b[i])) {
            return false;
        }
    }
    return true;
}

/* Ticks per unit for the duration literal suffixes.  A formula literal cannot
 * see a project calendar, so the default 8-hour day / 5-day week / 20-day month
 * is used (the same defaults SheetFormat carries). */
bool duration_unit_ticks(std::string_view unit, std::int64_t& ticks) noexcept {
    struct Unit {
        const char* name;
        std::int64_t ticks;
    };
    static constexpr Unit kUnits[] = {
        {"d", 4800},        {"day", 4800},      {"days", 4800},
        {"h", 600},         {"hr", 600},        {"hrs", 600},
        {"hour", 600},      {"hours", 600},     {"m", 10},
        {"min", 10},        {"mins", 10},       {"minute", 10},
        {"minutes", 10},    {"w", 24000},       {"wk", 24000},
        {"wks", 24000},     {"week", 24000},    {"weeks", 24000},
        {"mo", 96000},      {"mon", 96000},     {"mons", 96000},
        {"month", 96000},   {"months", 96000},
    };
    for (const Unit& candidate : kUnits) {
        if (equals_ci(unit, candidate.name)) {
            ticks = candidate.ticks;
            return true;
        }
    }
    return false;
}

void set_error(TokenizeResult& result, std::size_t position, std::string message) {
    result.ok = false;
    result.error_position = position;
    result.error = "offset " + std::to_string(position) + ": " + std::move(message);
}

bool scan_number(std::string_view source, std::size_t& pos, double& value) {
    const std::size_t start = pos;
    bool digits_before = false;
    bool digits_after = false;
    while (pos < source.size() && is_digit(source[pos])) {
        ++pos;
        digits_before = true;
    }
    if (pos < source.size() && source[pos] == '.') {
        ++pos;
        while (pos < source.size() && is_digit(source[pos])) {
            ++pos;
            digits_after = true;
        }
    }
    if (!digits_before && !digits_after) {
        pos = start;
        return false;
    }
    if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
        std::size_t lookahead = pos + 1;
        if (lookahead < source.size() && (source[lookahead] == '+' || source[lookahead] == '-')) {
            ++lookahead;
        }
        std::size_t digits = 0;
        while (lookahead < source.size() && is_digit(source[lookahead])) {
            ++lookahead;
            ++digits;
        }
        if (digits > 0) {
            pos = lookahead;
        }
    }
    const std::string piece(source.substr(start, pos - start));
    char* end = nullptr;
    const double parsed = std::strtod(piece.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        pos = start;
        return false;
    }
    value = parsed;
    return true;
}

}  // namespace

const char* token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::End: return "end of formula";
        case TokenKind::Number: return "number";
        case TokenKind::Text: return "text";
        case TokenKind::Date: return "date";
        case TokenKind::Duration: return "duration";
        case TokenKind::Bool: return "boolean";
        case TokenKind::FieldRef: return "field reference";
        case TokenKind::Identifier: return "name";
        case TokenKind::Plus: return "'+'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Star: return "'*'";
        case TokenKind::Slash: return "'/'";
        case TokenKind::Ampersand: return "'&'";
        case TokenKind::Equal: return "'='";
        case TokenKind::NotEqual: return "'<>'";
        case TokenKind::Less: return "'<'";
        case TokenKind::LessEqual: return "'<='";
        case TokenKind::Greater: return "'>'";
        case TokenKind::GreaterEqual: return "'>='";
        case TokenKind::LParen: return "'('";
        case TokenKind::RParen: return "')'";
        case TokenKind::Comma: return "','";
    }
    return "token";
}

TokenizeResult tokenize(std::string_view source) {
    TokenizeResult result;
    std::size_t pos = 0;
    while (pos < source.size()) {
        const char c = source[pos];
        if (is_space(c)) {
            ++pos;
            continue;
        }
        Token token;
        token.position = pos;
        if (is_digit(c) || (c == '.' && pos + 1 < source.size() && is_digit(source[pos + 1]))) {
            double value = 0.0;
            if (!scan_number(source, pos, value)) {
                set_error(result, token.position, "malformed number");
                return result;
            }
            /* A number immediately followed by a duration unit word is a
             * duration literal: 2d, 1.5 days, 4h, 30m, 2w, 1mo. */
            std::size_t after_number = pos;
            std::size_t unit_start = pos;
            while (unit_start < source.size() && (source[unit_start] == ' ' || source[unit_start] == '\t')) {
                ++unit_start;
            }
            std::size_t unit_end = unit_start;
            while (unit_end < source.size() && is_alpha(source[unit_end])) {
                ++unit_end;
            }
            std::int64_t unit_ticks = 0;
            if (unit_end > unit_start &&
                duration_unit_ticks(source.substr(unit_start, unit_end - unit_start), unit_ticks)) {
                const double ticks = value * static_cast<double>(unit_ticks);
                token.kind = TokenKind::Duration;
                token.ticks = static_cast<std::int64_t>(ticks < 0.0 ? ticks - 0.5 : ticks + 0.5);
                pos = unit_end;
            } else {
                token.kind = TokenKind::Number;
                token.number = value;
                pos = after_number;
            }
            result.tokens.push_back(std::move(token));
            continue;
        }
        if (is_alpha(c)) {
            const std::size_t start = pos;
            while (pos < source.size() && is_alnum(source[pos])) {
                ++pos;
            }
            token.text = std::string(source.substr(start, pos - start));
            if (equals_ci(token.text, "true") || equals_ci(token.text, "false")) {
                token.kind = TokenKind::Bool;
                token.boolean = equals_ci(token.text, "true");
            } else {
                token.kind = TokenKind::Identifier;
            }
            result.tokens.push_back(std::move(token));
            continue;
        }
        if (c == '"') {
            const std::size_t start = pos;
            ++pos;
            std::string text;
            bool closed = false;
            while (pos < source.size()) {
                if (source[pos] == '"') {
                    if (pos + 1 < source.size() && source[pos + 1] == '"') {
                        text.push_back('"');
                        pos += 2;
                        continue;
                    }
                    ++pos;
                    closed = true;
                    break;
                }
                text.push_back(source[pos]);
                ++pos;
            }
            if (!closed) {
                set_error(result, start, "unterminated string literal");
                return result;
            }
            token.kind = TokenKind::Text;
            token.text = std::move(text);
            result.tokens.push_back(std::move(token));
            continue;
        }
        if (c == '#') {
            const std::size_t start = pos;
            ++pos;
            const std::size_t close = source.find('#', pos);
            if (close == std::string_view::npos) {
                set_error(result, start, "unterminated date literal");
                return result;
            }
            const std::string text(source.substr(pos, close - pos));
            core::view::DateValue date;
            if (!core::view::parse_iso_datetime(text, date)) {
                set_error(result, start, "invalid date literal \"" + text + "\"");
                return result;
            }
            token.kind = TokenKind::Date;
            token.date = date;
            token.text = text;
            pos = close + 1;
            result.tokens.push_back(std::move(token));
            continue;
        }
        if (c == '[') {
            const std::size_t start = pos;
            ++pos;
            const std::size_t close = source.find(']', pos);
            if (close == std::string_view::npos) {
                set_error(result, start, "unterminated field reference");
                return result;
            }
            std::string text(source.substr(pos, close - pos));
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && is_space(text[begin])) {
                ++begin;
            }
            while (end > begin && is_space(text[end - 1])) {
                --end;
            }
            text = text.substr(begin, end - begin);
            if (text.empty()) {
                set_error(result, start, "empty field reference");
                return result;
            }
            token.kind = TokenKind::FieldRef;
            token.text = std::move(text);
            pos = close + 1;
            result.tokens.push_back(std::move(token));
            continue;
        }
        TokenKind kind = TokenKind::End;
        std::size_t length = 1;
        switch (c) {
            case '+': kind = TokenKind::Plus; break;
            case '-': kind = TokenKind::Minus; break;
            case '*': kind = TokenKind::Star; break;
            case '/': kind = TokenKind::Slash; break;
            case '&': kind = TokenKind::Ampersand; break;
            case '(': kind = TokenKind::LParen; break;
            case ')': kind = TokenKind::RParen; break;
            case ',': kind = TokenKind::Comma; break;
            case '=': kind = TokenKind::Equal; break;
            case '<':
                if (pos + 1 < source.size() && source[pos + 1] == '=') {
                    kind = TokenKind::LessEqual;
                    length = 2;
                } else if (pos + 1 < source.size() && source[pos + 1] == '>') {
                    kind = TokenKind::NotEqual;
                    length = 2;
                } else {
                    kind = TokenKind::Less;
                }
                break;
            case '>':
                if (pos + 1 < source.size() && source[pos + 1] == '=') {
                    kind = TokenKind::GreaterEqual;
                    length = 2;
                } else {
                    kind = TokenKind::Greater;
                }
                break;
            default:
                set_error(result, pos, std::string("unexpected character '") + c + "'");
                return result;
        }
        token.kind = kind;
        pos += length;
        result.tokens.push_back(std::move(token));
    }
    Token end_token;
    end_token.kind = TokenKind::End;
    end_token.position = source.size();
    result.tokens.push_back(std::move(end_token));
    result.ok = true;
    return result;
}

const char* unary_op_name(UnaryOp op) noexcept {
    switch (op) {
        case UnaryOp::Negate: return "-";
        case UnaryOp::Plus: return "+";
        case UnaryOp::Not: return "Not";
    }
    return "?";
}

const char* binary_op_name(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::Concat: return "&";
        case BinaryOp::Equal: return "=";
        case BinaryOp::NotEqual: return "<>";
        case BinaryOp::Less: return "<";
        case BinaryOp::LessEqual: return "<=";
        case BinaryOp::Greater: return ">";
        case BinaryOp::GreaterEqual: return ">=";
        case BinaryOp::And: return "And";
        case BinaryOp::Or: return "Or";
    }
    return "?";
}

const char* expr_kind_name(ExprKind kind) noexcept {
    switch (kind) {
        case ExprKind::Number: return "number";
        case ExprKind::Text: return "text";
        case ExprKind::Date: return "date";
        case ExprKind::Duration: return "duration";
        case ExprKind::Bool: return "boolean";
        case ExprKind::FieldRef: return "field reference";
        case ExprKind::Unary: return "unary";
        case ExprKind::Binary: return "binary";
        case ExprKind::Call: return "call";
    }
    return "?";
}

/* Arity of every function the evaluator implements.  -1 means unknown. */
int function_arity(std::string_view name) noexcept {
    if (name == "iif") return 3;
    if (name == "projdurvalue") return 1;
    if (name == "projdurconv") return 2;
    if (name == "projdateadd") return 2;
    if (name == "projdatesub") return 2;
    if (name == "projdatediff") return 2;
    if (name == "datevalue") return 1;
    if (name == "day") return 1;
    if (name == "month") return 1;
    if (name == "year") return 1;
    if (name == "hour") return 1;
    if (name == "minute") return 1;
    if (name == "weekday") return 1;
    if (name == "isnull") return 1;
    return -1;
}

namespace {

class Parser {
  public:
    Parser(std::vector<Token> tokens, core::model::RecordType record_type, ParseResult& result)
        : tokens_(std::move(tokens)),
          record_type_(record_type),
          result_(result) {}

    bool run() {
        Expr root = parse_or();
        if (failed_) {
            return false;
        }
        if (peek().kind != TokenKind::End) {
            fail(peek().position,
                 std::string("unexpected ") + token_kind_name(peek().kind) + " after the formula");
            return false;
        }
        result_.root = std::move(root);
        result_.ok = true;
        return true;
    }

  private:
    const Token& peek(std::size_t ahead = 0) const {
        const std::size_t index = index_ + ahead;
        if (index >= tokens_.size()) {
            return tokens_.back();
        }
        return tokens_[index];
    }

    bool accept(TokenKind kind) {
        if (peek().kind != kind) {
            return false;
        }
        ++index_;
        return true;
    }

    void fail(std::size_t position, std::string message) {
        if (failed_) {
            return;
        }
        failed_ = true;
        result_.ok = false;
        result_.error_position = position;
        result_.error = "offset " + std::to_string(position) + ": " + std::move(message);
    }

    Expr leaf(ExprKind kind, const Token& token) {
        Expr expr;
        expr.kind = kind;
        expr.position = token.position;
        return expr;
    }

    Expr parse_or() {
        Expr left = parse_and();
        while (!failed_) {
            BinaryOp op = BinaryOp::Or;
            if (peek().kind == TokenKind::Identifier && equals_ci(peek().text, "or")) {
                op = BinaryOp::Or;
            } else {
                break;
            }
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_and();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = op;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_and() {
        Expr left = parse_compare();
        while (!failed_) {
            if (!(peek().kind == TokenKind::Identifier && equals_ci(peek().text, "and"))) {
                break;
            }
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_compare();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = BinaryOp::And;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_compare() {
        Expr left = parse_concat();
        while (!failed_) {
            BinaryOp op = BinaryOp::Equal;
            switch (peek().kind) {
                case TokenKind::Equal: op = BinaryOp::Equal; break;
                case TokenKind::NotEqual: op = BinaryOp::NotEqual; break;
                case TokenKind::Less: op = BinaryOp::Less; break;
                case TokenKind::LessEqual: op = BinaryOp::LessEqual; break;
                case TokenKind::Greater: op = BinaryOp::Greater; break;
                case TokenKind::GreaterEqual: op = BinaryOp::GreaterEqual; break;
                default: return left;
            }
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_concat();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = op;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_concat() {
        Expr left = parse_additive();
        while (!failed_ && peek().kind == TokenKind::Ampersand) {
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_additive();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = BinaryOp::Concat;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_additive() {
        Expr left = parse_multiplicative();
        while (!failed_ && (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus)) {
            const BinaryOp op =
                peek().kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract;
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_multiplicative();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = op;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_multiplicative() {
        Expr left = parse_unary();
        while (!failed_ && (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash)) {
            const BinaryOp op = peek().kind == TokenKind::Star ? BinaryOp::Multiply : BinaryOp::Divide;
            const std::size_t position = peek().position;
            ++index_;
            Expr right = parse_unary();
            if (failed_) {
                return left;
            }
            Expr node;
            node.kind = ExprKind::Binary;
            node.binary = op;
            node.position = position;
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    Expr parse_unary() {
        UnaryOp op = UnaryOp::Negate;
        bool is_unary = true;
        if (peek().kind == TokenKind::Minus) {
            op = UnaryOp::Negate;
        } else if (peek().kind == TokenKind::Plus) {
            op = UnaryOp::Plus;
        } else if (peek().kind == TokenKind::Identifier && equals_ci(peek().text, "not")) {
            op = UnaryOp::Not;
        } else {
            is_unary = false;
        }
        if (!is_unary) {
            return parse_primary();
        }
        const std::size_t position = peek().position;
        ++index_;
        Expr operand = parse_unary();
        if (failed_) {
            return operand;
        }
        Expr node;
        node.kind = ExprKind::Unary;
        node.unary = op;
        node.position = position;
        node.children.push_back(std::move(operand));
        return node;
    }

    Expr parse_primary() {
        const Token& token = peek();
        switch (token.kind) {
            case TokenKind::Number: {
                Expr expr = leaf(ExprKind::Number, token);
                expr.number = token.number;
                ++index_;
                return expr;
            }
            case TokenKind::Text: {
                Expr expr = leaf(ExprKind::Text, token);
                expr.text = token.text;
                ++index_;
                return expr;
            }
            case TokenKind::Date: {
                Expr expr = leaf(ExprKind::Date, token);
                expr.date = token.date;
                expr.text = token.text;
                ++index_;
                return expr;
            }
            case TokenKind::Duration: {
                Expr expr = leaf(ExprKind::Duration, token);
                expr.ticks = token.ticks;
                ++index_;
                return expr;
            }
            case TokenKind::Bool: {
                Expr expr = leaf(ExprKind::Bool, token);
                expr.boolean = token.boolean;
                ++index_;
                return expr;
            }
            case TokenKind::FieldRef: {
                FieldId field = resolve_field_token(token.text, record_type_);
                if (field == 0) {
                    fail(token.position, "unknown field reference \"" + token.text + "\"");
                    return Expr{};
                }
                Expr expr = leaf(ExprKind::FieldRef, token);
                expr.field = field;
                expr.text = token.text;
                result_.field_refs.push_back(FieldRef{field, token.position, token.text});
                ++index_;
                return expr;
            }
            case TokenKind::Identifier: {
                if (peek(1).kind != TokenKind::LParen) {
                    fail(token.position,
                         "unknown name \"" + token.text +
                             "\" (a field reference is written [Name])");
                    return Expr{};
                }
                return parse_call();
            }
            case TokenKind::LParen: {
                ++index_;
                Expr inner = parse_or();
                if (failed_) {
                    return inner;
                }
                if (!accept(TokenKind::RParen)) {
                    fail(peek().position,
                         std::string("expected ')' but found ") + token_kind_name(peek().kind));
                    return inner;
                }
                return inner;
            }
            default:
                fail(token.position,
                     std::string("expected an expression but found ") +
                         token_kind_name(token.kind));
                return Expr{};
        }
    }

    Expr parse_call() {
        const Token name_token = peek();
        const std::string name = lower_ascii(name_token.text);
        const std::size_t position = name_token.position;
        ++index_;  /* the identifier */
        ++index_;  /* the '(' the caller checked for */
        std::vector<Expr> args;
        if (peek().kind != TokenKind::RParen) {
            while (!failed_) {
                Expr argument = parse_or();
                if (failed_) {
                    return Expr{};
                }
                args.push_back(std::move(argument));
                if (accept(TokenKind::Comma)) {
                    continue;
                }
                break;
            }
        }
        if (!accept(TokenKind::RParen)) {
            fail(peek().position,
                 std::string("expected ')' but found ") + token_kind_name(peek().kind));
            return Expr{};
        }
        const int arity = function_arity(name);
        if (arity < 0) {
            fail(position, "unknown function \"" + name_token.text + "\"");
            return Expr{};
        }
        if (args.size() != static_cast<std::size_t>(arity)) {
            fail(position, "function " + name_token.text + " takes " + std::to_string(arity) +
                               " argument(s), got " + std::to_string(args.size()));
            return Expr{};
        }
        Expr expr;
        expr.kind = ExprKind::Call;
        expr.position = position;
        expr.text = name;
        expr.children = std::move(args);
        return expr;
    }

    [[maybe_unused]] std::vector<Token> tokens_;
    core::model::RecordType record_type_;
    ParseResult& result_;
    std::size_t index_ = 0;
    bool failed_ = false;
};

}  // namespace

ParseResult parse(std::string_view source, core::model::RecordType record_type) {
    ParseResult result;
    TokenizeResult tokens = tokenize(source);
    if (!tokens.ok) {
        result.ok = false;
        result.error_position = tokens.error_position;
        result.error = std::move(tokens.error);
        return result;
    }
    Parser parser(std::move(tokens.tokens), record_type, result);
    parser.run();
    return result;
}

}  // namespace core::formula
