#include "eggc/expr.hpp"
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace eggc {
ExprId RecExpr::root() const {
    if (nodes.empty()) throw std::logic_error("empty expression has no root");
    return {static_cast<std::uint32_t>(nodes.size() - 1)};
}

namespace {
struct Token {
    enum class Kind { Atom, Left, Right } kind;
    std::string value;
    std::size_t offset;
};

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (source[i] == '(') { tokens.push_back({Token::Kind::Left, "(", i++}); continue; }
        if (source[i] == ')') { tokens.push_back({Token::Kind::Right, ")", i++}); continue; }
        const auto start = i;
        std::string value;
        if (source[i] == '"') {
            ++i;
            bool closed = false;
            while (i < source.size()) {
                char ch = source[i++];
                if (ch == '"') { closed = true; break; }
                if (ch == '\\') {
                    if (i == source.size()) throw std::invalid_argument("unterminated escape at byte " + std::to_string(i - 1));
                    ch = source[i++];
                    switch (ch) {
                        case 'n': value.push_back('\n'); break;
                        case 'r': value.push_back('\r'); break;
                        case 't': value.push_back('\t'); break;
                        case '\\': value.push_back('\\'); break;
                        case '"': value.push_back('"'); break;
                        default: throw std::invalid_argument("invalid escape at byte " + std::to_string(i - 2));
                    }
                } else value.push_back(ch);
            }
            if (!closed) throw std::invalid_argument("unterminated quoted atom at byte " + std::to_string(start));
            if (i < source.size() && !std::isspace(static_cast<unsigned char>(source[i])) &&
                source[i] != '(' && source[i] != ')')
                throw std::invalid_argument("expected a token boundary at byte " + std::to_string(i));
        } else {
            while (i < source.size() && !std::isspace(static_cast<unsigned char>(source[i])) &&
                   source[i] != '(' && source[i] != ')') value.push_back(source[i++]);
        }
        tokens.push_back({Token::Kind::Atom, std::move(value), start});
    }
    return tokens;
}

struct Frame { std::string op; std::vector<ExprId> children; std::size_t offset; };

std::string print_atom(const std::string& atom) {
    bool quote = atom.empty();
    for (const unsigned char c : atom)
        if (std::isspace(c) || c == '(' || c == ')' || c == '"' || c == '\\') quote = true;
    if (!quote) return atom;
    std::string result = "\"";
    for (const char c : atom) {
        switch (c) {
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            default: result.push_back(c); break;
        }
    }
    return result + '"';
}
}

RecExpr parse_expr(std::string_view text) {
    const auto tokens = tokenize(text);
    std::vector<Frame> stack;
    RecExpr result;
    bool have_root = false;
    auto append = [&](ExprId id, std::size_t offset) {
        (void)id;
        if (stack.empty()) {
            if (have_root) throw std::invalid_argument("unexpected second expression at byte " + std::to_string(offset));
            have_root = true;
        } else stack.back().children.push_back(id);
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.kind == Token::Kind::Left) {
            if (i + 1 >= tokens.size() || tokens[i + 1].kind != Token::Kind::Atom)
                throw std::invalid_argument("expected operator after '(' at byte " + std::to_string(token.offset));
            stack.push_back({tokens[++i].value, {}, token.offset});
            continue;
        }
        if (token.kind == Token::Kind::Right) {
            if (stack.empty()) throw std::invalid_argument("unexpected ')' at byte " + std::to_string(token.offset));
            auto frame = std::move(stack.back());
            stack.pop_back();
            const auto id = ExprId{static_cast<std::uint32_t>(result.nodes.size())};
            result.nodes.push_back({std::move(frame.op), std::move(frame.children)});
            append(id, token.offset);
            continue;
        }

        if (!stack.empty() && stack.back().op.empty())
            throw std::invalid_argument("empty operator at byte " + std::to_string(token.offset));
        const auto id = ExprId{static_cast<std::uint32_t>(result.nodes.size())};
        result.nodes.push_back({token.value, {}});
        append(id, token.offset);
    }
    if (!stack.empty()) throw std::invalid_argument("missing ')' for list at byte " + std::to_string(stack.back().offset));
    if (!have_root) throw std::invalid_argument("expected an expression");
    return result;
}

std::string to_string(const RecExpr& expr) {
    if (expr.nodes.empty()) throw std::invalid_argument("cannot print an empty expression");
    const auto root = expr.root();
    for (std::size_t i = 0; i < expr.nodes.size(); ++i)
        for (const auto child : expr.nodes[i].children)
            if (child.value >= i) throw std::invalid_argument("expression children must refer to earlier nodes");

    struct Item { enum class Kind { Node, Close, Space } kind; ExprId id; };
    std::vector<Item> stack{{Item::Kind::Node, root}};
    std::ostringstream out;
    while (!stack.empty()) {
        const auto item = stack.back();
        stack.pop_back();
        if (item.kind == Item::Kind::Close) { out << ')'; continue; }
        if (item.kind == Item::Kind::Space) { out << ' '; continue; }
        const auto& node = expr.nodes[item.id.value];
        if (node.children.empty()) { out << print_atom(node.op); continue; }
        out << '(' << print_atom(node.op);
        stack.push_back({Item::Kind::Close, {0}});
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            stack.push_back({Item::Kind::Node, *it});
            stack.push_back({Item::Kind::Space, {0}});
        }
    }
    return out.str();
}

std::string to_string(const Expr& expr) {
    std::ostringstream out;
    if (expr.children.empty()) {
        out << print_atom(expr.op);
        return out.str();
    }
    out << '(' << print_atom(expr.op);
    for (const auto& child : expr.children) out << ' ' << to_string(child);
    return out.str() + ')';
}
}
