use egg::{define_language, rewrite, AstSize, Extractor, Id, RecExpr, Runner, Symbol};

define_language! {
    enum Math {
        Num(i64),
        "+" = Add([Id; 2]),
        "*" = Mul([Id; 2]),
        Symbol(Symbol),
    }
}

fn main() {
    let root_expr: RecExpr<Math> = "(+ (* 2 3) (+ x 0))".parse().unwrap();
    let expected_expr: RecExpr<Math> = "(+ (* 2 3) x)".parse().unwrap();
    let rewrites = vec![
        rewrite!("add-zero"; "(+ ?x 0)" => "?x"),
        rewrite!("mul-one"; "(* ?x 1)" => "?x"),
    ];
    let runner = Runner::<Math, ()>::default()
        .with_iter_limit(8)
        .with_expr(&root_expr)
        .run(&rewrites);
    let root = runner.egraph.lookup_expr(&root_expr).unwrap();
    let expected = runner.egraph.lookup_expr(&expected_expr).unwrap();
    let extractor = Extractor::new(&runner.egraph, AstSize);
    let (cost, _) = extractor.find_best(root);
    println!("equivalent={} cost={}", if root == expected { 1 } else { 0 }, cost);
}
