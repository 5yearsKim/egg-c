use egg::{
    define_language, Analysis, AstDepth, AstSize, ConditionalApplier, DidMerge, EGraph,
    Extractor, Id, Pattern, RecExpr, Rewrite, Runner, Searcher, StopReason, Subst, Symbol, Var,
};
use std::collections::BTreeMap;
use std::io::{self, BufRead};

define_language! {
    enum Math {
        Num(i64),
        "+" = Add([Id; 2]),
        "*" = Mul([Id; 2]),
        "/" = Div([Id; 2]),
        "pair" = Pair([Id; 2]),
        "f" = F(Id),
        "g" = G(Id),
        Symbol(Symbol),
    }
}

#[derive(Default)]
struct Fold;

impl Analysis<Math> for Fold {
    type Data = Option<i64>;

    fn make(egraph: &EGraph<Math, Self>, node: &Math) -> Self::Data {
        match node {
            Math::Num(n) => Some(*n),
            Math::Add([a, b]) => egraph[*a].data?.checked_add(egraph[*b].data?),
            Math::Mul([a, b]) => egraph[*a].data?.checked_mul(egraph[*b].data?),
            _ => None,
        }
    }

    fn merge(&mut self, into: &mut Self::Data, from: Self::Data) -> DidMerge {
        match (*into, from) {
            (None, Some(n)) => {
                *into = Some(n);
                DidMerge(true, false)
            }
            (Some(_), None) => DidMerge(false, true),
            (Some(a), Some(b)) if a != b => panic!("conflicting constants: {a} != {b}"),
            _ => DidMerge(false, false),
        }
    }

    fn modify(egraph: &mut EGraph<Math, Self>, id: Id) {
        if let Some(value) = egraph[id].data {
            let literal = egraph.add(Math::Num(value));
            egraph.union(id, literal);
        }
    }
}

fn signature(graph: &EGraph<Math, Fold>, id: Id, handles: &BTreeMap<String, Id>) -> String {
    let mut result = String::new();
    for (name, handle) in handles {
        if graph.find(id) == graph.find(*handle) {
            result.push_str(name);
            result.push(',');
        }
    }
    result
}

fn match_result(
    graph: &EGraph<Math, Fold>,
    root: Id,
    pattern: &Pattern<Math>,
    handles: &BTreeMap<String, Id>,
) -> String {
    let mut vars = pattern.vars();
    vars.sort_by_key(|var| var.to_string());
    let mut matches = Vec::new();
    if let Some(found) = pattern.search_eclass(graph, root) {
        for subst in found.substs {
            let mut item = String::new();
            for var in &vars {
                item.push_str(&format!("{}={};", var, signature(graph, subst[*var], handles)));
            }
            matches.push(item);
        }
    }
    matches.sort();
    matches.dedup();
    let mut result = format!("match\t{}", matches.len());
    for item in matches {
        result.push('\t');
        result.push_str(&item);
    }
    result
}

fn rule(name: &str, lhs: &str, rhs: &str, condition: &str) -> Rewrite<Math, Fold> {
    let lhs: Pattern<Math> = lhs.parse().unwrap();
    let rhs: Pattern<Math> = rhs.parse().unwrap();
    if condition == "-" {
        Rewrite::new(name, lhs, rhs).unwrap()
    } else {
        let variable: Var = condition.strip_prefix("nonzero:").unwrap().parse().unwrap();
        let check = move |graph: &mut EGraph<Math, Fold>, _: Id, subst: &Subst| {
            matches!(graph[subst[variable]].data, Some(value) if value != 0)
        };
        Rewrite::new(name, lhs, ConditionalApplier { condition: check, applier: rhs }).unwrap()
    }
}

fn replay() {
    let mut graph = EGraph::<Math, Fold>::default();
    let mut handles = BTreeMap::<String, Id>::new();
    let mut rules = Vec::<Rewrite<Math, Fold>>::new();
    for (line_index, line) in io::stdin().lock().lines().enumerate() {
        let line = line.unwrap();
        if line.is_empty() || line.starts_with('#') { continue; }
        let fields: Vec<&str> = line.split('\t').collect();
        let command = fields[0];
        match command {
            "version" => assert_eq!(fields, ["version", "1"]),
            "add" => {
                assert_eq!(fields.len(), 3);
                assert!(!handles.contains_key(fields[1]));
                let expr: RecExpr<Math> = fields[2].parse().unwrap();
                handles.insert(fields[1].to_string(), graph.add_expr(&expr));
            }
            "merge" => {
                assert_eq!(fields.len(), 3);
                graph.union(handles[fields[1]], handles[fields[2]]);
            }
            "rebuild" => { assert_eq!(fields.len(), 1); graph.rebuild(); }
            "eq" => {
                assert_eq!(fields.len(), 3);
                println!("eq\t{}", if graph.find(handles[fields[1]]) == graph.find(handles[fields[2]]) { 1 } else { 0 });
            }
            "cost" => {
                assert_eq!(fields.len(), 3);
                let root = handles[fields[1]];
                let cost = match fields[2] {
                    "size" => Extractor::new(&graph, AstSize).find_best(root).0,
                    "depth" => Extractor::new(&graph, AstDepth).find_best(root).0,
                    _ => panic!("unknown cost policy"),
                };
                println!("cost\t{cost}");
            }
            "fact" => {
                assert_eq!(fields.len(), 2);
                match graph[handles[fields[1]]].data {
                    Some(value) => println!("fact\tK:{value}"),
                    None => println!("fact\tU"),
                }
            }
            "witness" => {
                assert_eq!(fields.len(), 2);
                let root = handles[fields[1]];
                let expression = Extractor::new(&graph, AstSize).find_best(root).1;
                let witness = graph.add_expr(&expression);
                println!("witness\t{}", if graph.find(witness) == graph.find(root) { 1 } else { 0 });
            }
            "match" => {
                assert_eq!(fields.len(), 3);
                let pattern: Pattern<Math> = fields[2].parse().unwrap();
                println!("{}", match_result(&graph, handles[fields[1]], &pattern, &handles));
            }
            "rule" => {
                assert_eq!(fields.len(), 5);
                rules.push(rule(fields[1], fields[2], fields[3], fields[4]));
            }
            "run" => {
                assert_eq!(fields.len(), 2);
                let current = std::mem::take(&mut graph);
                let runner = Runner::<Math, Fold>::default()
                    .with_egraph(current)
                    .with_iter_limit(fields[1].parse().unwrap())
                    .with_node_limit(100000)
                    .run(&rules);
                println!("run\t{}", if matches!(runner.stop_reason, Some(StopReason::Saturated)) { "sat" } else { "limit" });
                graph = runner.egraph;
            }
            _ => panic!("line {}: unknown operation {}", line_index + 1, command),
        }
    }
}

fn main() { replay(); }
