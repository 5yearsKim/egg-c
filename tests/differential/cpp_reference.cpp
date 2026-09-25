#include "eggc/constant_analysis.hpp"
#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace eggc;

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto tab = line.find('\t', start);
        result.push_back(line.substr(start, tab - start));
        if (tab == std::string::npos) return result;
        start = tab + 1;
    }
}

std::string signature(const EGraph& graph, Id id, const std::map<std::string, Id>& handles) {
    std::string result;
    for (const auto& [name, handle] : handles)
        if (graph.find(id) == graph.find(handle)) result += name + ',';
    return result;
}

std::string match_result(const EGraph& graph, Id root, const Pattern& pattern,
                         const std::map<std::string, Id>& handles) {
    std::vector<std::string> normalized;
    for (const auto& subst : match(graph, pattern, root)) {
        std::map<std::string, Id> sorted(subst.begin(), subst.end());
        std::string item;
        for (const auto& [var, id] : sorted)
            item += var + '=' + signature(graph, id, handles) + ';';
        normalized.push_back(std::move(item));
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    std::string result = "match\t" + std::to_string(normalized.size());
    for (const auto& item : normalized) result += "\t" + item;
    return result;
}

void require_fields(const std::vector<std::string>& parts, std::size_t count) {
    if (parts.size() != count) throw std::invalid_argument("wrong field count");
}

void replay() {
    EGraph graph(std::make_shared<ConstantAnalysis>());
    std::map<std::string, Id> handles;
    std::vector<Rewrite> rules;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(std::cin, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        const auto parts = fields(line);
        try {
            const auto& op = parts[0];
            if (op == "version") {
                require_fields(parts, 2);
                if (parts[1] != "1") throw std::invalid_argument("unsupported protocol version");
            } else if (op == "add") {
                require_fields(parts, 3);
                if (handles.count(parts[1])) throw std::invalid_argument("duplicate handle");
                handles.emplace(parts[1], graph.add_expr(parse_expr(parts[2])));
            } else if (op == "merge") {
                require_fields(parts, 3);
                graph.merge(handles.at(parts[1]), handles.at(parts[2]));
            } else if (op == "rebuild") {
                require_fields(parts, 1);
                graph.rebuild();
            } else if (op == "eq") {
                require_fields(parts, 3);
                graph.require_clean();
                std::cout << "eq\t" << (graph.find(handles.at(parts[1])) ==
                                      graph.find(handles.at(parts[2]))) << '\n';
            } else if (op == "cost") {
                require_fields(parts, 3);
                const auto policy = parts[2] == "size" ? ast_size_cost() : ast_depth_cost();
                if (parts[2] != "size" && parts[2] != "depth")
                    throw std::invalid_argument("unknown cost policy");
                std::cout << "cost\t" << Extractor(graph, policy).best_cost(handles.at(parts[1])) << '\n';
            } else if (op == "fact") {
                require_fields(parts, 2);
                graph.require_clean();
                const auto fact = std::any_cast<ConstantFact>(graph.analysis_data(handles.at(parts[1])));
                std::cout << "fact\t";
                if (fact.kind == ConstantFact::Kind::Known) std::cout << "K:" << fact.value;
                else if (fact.kind == ConstantFact::Kind::Unknown) std::cout << "U";
                else std::cout << "C";
                std::cout << '\n';
            } else if (op == "witness") {
                require_fields(parts, 2);
                const auto expression = Extractor(graph).find_best_rec_expr(handles.at(parts[1])).second;
                const auto witness = graph.add_expr(expression);
                std::cout << "witness\t" << (graph.find(witness) == graph.find(handles.at(parts[1]))) << '\n';
                graph.require_clean();
            } else if (op == "match") {
                require_fields(parts, 3);
                std::cout << match_result(graph, handles.at(parts[1]),
                                          parse_pattern(parts[2]), handles) << '\n';
            } else if (op == "rule") {
                require_fields(parts, 5);
                if (parts[4] == "-")
                    rules.push_back(parse_rewrite(parts[1], parts[2], parts[3]));
                else if (parts[4].rfind("nonzero:", 0) == 0)
                    rules.push_back(parse_rewrite(parts[1], parts[2], parts[3],
                                                  known_nonzero(parts[4].substr(8))));
                else throw std::invalid_argument("unknown condition");
            } else if (op == "run") {
                require_fields(parts, 2);
                RunOptions options;
                options.iteration_limit = std::stoul(parts[1]);
                options.node_limit = 100000;
                const auto report = run(graph, rules, options);
                std::cout << "run\t" << (report.reason == StopReason::Saturated ? "sat" : "limit") << '\n';
            } else throw std::invalid_argument("unknown operation");
        } catch (const std::exception& error) {
            throw std::runtime_error("line " + std::to_string(line_number) + ": " + error.what());
        }
    }
}
}

int main() {
    try { replay(); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
