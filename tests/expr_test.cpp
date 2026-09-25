#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include <iostream>
#include <stdexcept>

namespace {
bool check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool parsing_printing_and_adding() {
    const auto expr = eggc::parse_expr("(* (+ x 0) 1)");
    eggc::EGraph graph;
    const auto root = graph.add_expr(expr);
    graph.rebuild();
    const auto extracted = eggc::Extractor(graph).find_best_rec_expr(root);
    const auto printed = eggc::to_string(extracted.second);
    return check(expr.nodes.size() == 5, "parser creates a flat bottom-up expression") &&
           check(printed == "(* (+ x 0) 1)", "flat extraction prints the expression") &&
           check(eggc::parse_expr(printed).nodes.size() == 5, "printed expression parses again");
}

bool quoted_atoms_round_trip() {
    const std::string input = "(\"op with space\" \"x(y)\\\"z\" plain)";
    const auto expr = eggc::parse_expr(input);
    return check(eggc::to_string(expr) == input, "quoted operator and atom round trip");
}

bool pattern_and_rewrite_helpers_work() {
    const auto rule = eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x");
    bool rejects_unbound = false;
    try { (void)eggc::parse_rewrite("bad", "a", "?free"); }
    catch (const std::invalid_argument&) { rejects_unbound = true; }
    const auto parsed = eggc::parse_pattern("(pair ?x ?x)");
    return check(rule.name == "add-zero" && rule.lhs.children.size() == 2,
                 "rewrite helper parses both patterns") &&
           check(rejects_unbound, "rewrite helper validates RHS variables") &&
           check(!parsed.is_var() && parsed.children[0].is_var(), "pattern variables are recognized");
}

bool malformed_input_is_rejected() {
    bool empty = false, missing_close = false, trailing = false, bad_escape = false;
    try { (void)eggc::parse_expr("  "); } catch (const std::invalid_argument&) { empty = true; }
    try { (void)eggc::parse_expr("(+ x"); } catch (const std::invalid_argument&) { missing_close = true; }
    try { (void)eggc::parse_expr("x y"); } catch (const std::invalid_argument&) { trailing = true; }
    try { (void)eggc::parse_expr("\"x\\q\""); } catch (const std::invalid_argument&) { bad_escape = true; }
    return check(empty, "empty source is rejected") &&
           check(missing_close, "missing parenthesis is rejected") &&
           check(trailing, "trailing expression is rejected") &&
           check(bad_escape, "unknown escape is rejected");
}
}

int main() {
    const bool passed = parsing_printing_and_adding() && quoted_atoms_round_trip() &&
                        pattern_and_rewrite_helpers_work() && malformed_input_is_rejected();
    return passed ? 0 : 1;
}
