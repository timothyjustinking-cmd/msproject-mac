/*
 * expr.h -- tokenizer, AST and parser for MS Project's custom-field formula
 * language.
 *
 * WHAT IS MEASURED, WHAT IS INFERRED
 *   Measured (docs/analysis/oracle/w10-oracle.md section 2): a project file
 *   stores a formula in 114/Props as UTF-16 text with every field reference
 *   rewritten to a "[MSPJ<decimal PjField id>]" token --
 *       [Duration]*2      ->  [MSPJ188744967]*2
 *       [% Complete]>50,1,0 -> [MSPJ188743712]>50,1,0
 *       [Name] & "-" & [Duration] -> [MSPJ...] & "-" & [MSPJ188744967]
 *   188744967 is Task.DurationText (0x0b400507, see fields.h) and 188743712 is
 *   Task.PercentComplete (0x0b400020).  Both tokens are accepted here, and a
 *   human field name in brackets is resolved through the descriptor table.
 *
 *   Inferred (no oracle run evaluates a formula; Project does not compute
 *   custom fields into a headless GetField, so nothing below is measured):
 *   precedence, operator spellings, string escaping and the set of function
 *   names.  They follow Project's documented formula language and VBA-like
 *   conventions.  The parser is deliberately strict: an unknown field name or
 *   function is a positioned error, never a silently blank value.
 *
 * GRAMMAR (lowest precedence first; all binaries are left associative)
 *
 *   expression := or
 *   or         := and (("Or" | "||") and)*
 *   and        := compare (("And" | "&&") compare)*
 *   compare    := concat (("=" | "<>" | "<" | "<=" | ">" | ">=") concat)*
 *   concat     := additive ("&" additive)*
 *   additive   := multiplicative (("+" | "-") multiplicative)*
 *   multiplicative := unary (("*" | "/") unary)*
 *   unary      := ("-" | "+" | "Not") unary | primary
 *   primary    := number | string | date | duration | bool | field-ref
 *               | identifier "(" [ expression ("," expression)* ] ")"
 *               | "(" expression ")"
 *
 * LITERALS
 *   number      123, 1.5, .5, 1e3
 *   string      "text"; a doubled quote ("" ) is one embedded quote
 *   date        #2026-10-26# or #2026-10-26T08:00# (ISO order only; the '#'
 *               delimiter is our choice -- see eval.h)
 *   duration    2d, 1.5 days, 4h, 30m, 2w, 1mo (unit words are
 *               case-insensitive; a bare number is NOT a duration)
 *   bool        True, False
 *   field-ref   [Name], [% Complete], [MSPJ188744967]
 *
 * All positions are 0-based byte offsets into the source text, so a caller can
 * point at the offending character.
 */

#ifndef CORE_FORMULA_EXPR_H
#define CORE_FORMULA_EXPR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "field_table.h"
#include "value.h"

namespace core::formula {

using core::view::FieldId;

/*
 * Hard bound on expression nesting, shared by the parser and the evaluator.
 * Nesting UP TO AND INCLUDING kMaxFormulaDepth is accepted; kMaxFormulaDepth+1
 * is the first refused level.
 *
 * WHY IT EXISTS (measured 2026-10-22, wave-13 fuzz stream): the parser is
 * recursive descent, so deeply nested input consumes stack in proportion to
 * depth.  On the MSVC Release build (1 MB stack) 256 nested IIf calls and 384
 * nested parentheses died with 0xC00000FD (STATUS_STACK_OVERFLOW); 128 of each
 * was fine.  Formula text comes from 114/Props, so a corrupt or adversarial
 * .mpp could crash the app merely by being opened.  Parsing REFUSES input deeper
 * than the bound with the usual positioned error, and the evaluator refuses an
 * AST deeper than the same bound.  Nothing here relies on exceptions.
 *
 * WHY 64 (ASan follow-up, same day): 160 was safe in Release but NOT under
 * AddressSanitizer, whose instrumented frames are much larger.  In
 * `build\vs-w57-asan` (Debug, /fsanitize=address) 64 and 72-deep input parsed
 * but 80/88/96/128/160 overflowed the stack before the guard fired.  64 is
 * below the ASan-measured good depth (72), so it is safe on every build and
 * thread, while real formulas are fewer than ten levels deep -- 160 bought
 * nothing but risk.  If this is ever raised, re-measure under ASan first.
 *
 * TWO GUARDS, ONE CONSTANT.  A flat "1 And 1 And ..." or "1+2+3+..." chain is
 * parsed iteratively, so long chains do not consume parser stack; but the tree
 * it builds is left-deep, and a deep tree's recursive DESTRUCTOR would also
 * overflow the stack (measured under ASan).  So the parser tracks each node's
 * tree height (`Expr::depth`) and refuses a chain whose TREE grows past the
 * bound as well -- parsed output is always shallow enough to destroy and to
 * evaluate.  The evaluator separately refuses a hand-built AST deeper than the
 * same bound (see eval.h).
 */
inline constexpr int kMaxFormulaDepth = 64;

/* ------------------------------------------------------------------------- */
/* Tokenizer                                                                  */
/* ------------------------------------------------------------------------- */

enum class TokenKind : std::uint8_t {
    End = 0,
    Number,
    Text,
    Date,
    Duration,
    Bool,
    FieldRef,
    Identifier,
    Plus,
    Minus,
    Star,
    Slash,
    Ampersand,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LParen,
    RParen,
    Comma,
};

const char* token_kind_name(TokenKind kind) noexcept;

struct Token {
    TokenKind kind = TokenKind::End;
    std::size_t position = 0;
    double number = 0.0;
    std::int64_t ticks = 0;            /* Duration literal, 6-second ticks */
    bool boolean = false;              /* Bool literal */
    core::view::DateValue date{};      /* Date literal */
    std::string text;                  /* string literal / identifier / field token */
};

struct TokenizeResult {
    bool ok = false;
    std::vector<Token> tokens;         /* always ends with an End token when ok */
    std::size_t error_position = 0;
    std::string error;
};

/*
 * Splits `source` into tokens.  The Duration literal unit suffixes use the
 * default 8-hour day only for naming; the tick count is the unit's tick count
 * (4800 ticks/day, 600/hour, 10/minute, 5 days/week, 20 days/month -- the same
 * defaults core::sheet::SheetFormat carries; a formula literal cannot see a
 * project calendar, so this is a documented choice).
 */
TokenizeResult tokenize(std::string_view source);

/* ------------------------------------------------------------------------- */
/* AST                                                                        */
/* ------------------------------------------------------------------------- */

enum class UnaryOp : std::uint8_t { Negate = 0, Plus, Not };

enum class BinaryOp : std::uint8_t {
    Add = 0,
    Subtract,
    Multiply,
    Divide,
    Concat,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
};

enum class ExprKind : std::uint8_t {
    Number = 0,
    Text,
    Date,
    Duration,
    Bool,
    FieldRef,
    Unary,
    Binary,
    Call,
};

const char* unary_op_name(UnaryOp op) noexcept;
const char* binary_op_name(BinaryOp op) noexcept;
const char* expr_kind_name(ExprKind kind) noexcept;

/*
 * Number of arguments the evaluator's function `name` (lower case) takes, or
 * -1 when the name is not a known function.  The parser enforces this at parse
 * time; the evaluator re-checks a hand-built AST.
 */
int function_arity(std::string_view name) noexcept;

/*
 * One AST node.  `children` is empty for leaves, one for Unary, two for Binary
 * and the argument list for Call.  The tree is a value type (no pointers), so
 * a parsed formula is trivially copyable and a test can walk it directly.
 */
struct Expr {
    ExprKind kind = ExprKind::Number;
    std::size_t position = 0;

    UnaryOp unary = UnaryOp::Negate;
    BinaryOp binary = BinaryOp::Add;

    double number = 0.0;               /* Number literal */
    std::int64_t ticks = 0;            /* Duration literal */
    bool boolean = false;              /* Bool literal */
    core::view::DateValue date{};      /* Date literal */

    std::string text;                  /* Text literal, field token, call name */
    FieldId field = 0;                 /* FieldRef: resolved PjField id */

    /*
     * Parse bookkeeping: the subtree height in EDGES (a leaf is 0), which is
     * also the evaluator's recursion depth for that node.  The parser refuses
     * to finish a node whose depth exceeds kMaxFormulaDepth, so a returned AST
     * is shallow enough that its recursive destructor and the evaluator are safe
     * under AddressSanitizer.  The evaluator ignores this field.
     */
    int depth = 0;

    std::vector<Expr> children;
};

/* One "[...]" reference the parser resolved, in source order. */
struct FieldRef {
    FieldId field = 0;
    std::size_t position = 0;
    std::string token;                 /* the text between the brackets */
};

struct ParseResult {
    bool ok = false;
    Expr root;
    std::vector<FieldRef> field_refs;
    std::size_t error_position = 0;
    std::string error;                 /* "offset N: message" when !ok */
};

/*
 * Parses one formula.  Field names are resolved against the descriptor table
 * scoped to `record_type` (default Task).  Unknown names and functions are
 * errors with a position.
 */
ParseResult parse(std::string_view source,
                  core::model::RecordType record_type = core::model::RecordType::Task);

}  // namespace core::formula

#endif  // CORE_FORMULA_EXPR_H
