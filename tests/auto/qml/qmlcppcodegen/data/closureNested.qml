pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property list<int> factors: [1, 10]
    property int calls: 0

    // The outer callback needs a context of its own for "n", which the inner one captures.
    function products(): real {
        let s = 0
        root.numbers.forEach(n => {
            root.factors.forEach(f => { s += n * f })
        })
        return s
    }

    // A variable of the callback itself, captured and changed by a closure inside it. It is
    // a new variable for each call of the callback.
    function countPerElement(): int {
        let total = 0
        root.numbers.forEach(n => {
            let count = 0
            root.factors.forEach(() => { ++count })
            total = total * 10 + count
        })
        return total
    }

    // Three levels, each with variables captured further in
    function threeLevels(): real {
        let s = 0
        root.numbers.forEach(n => {
            root.factors.forEach(f => {
                root.factors.forEach(g => { s += n * f * g })
            })
        })
        return s
    }

    // The value a callback returns, computed by a closure inside it
    function anyProductAbove(limit: real): bool {
        return root.numbers.some(n => root.factors.some(f => n * f > limit))
    }

    // A function expression that does not use "this" or "arguments" is as good as an arrow
    // function.
    function functionExpression(): real {
        let s = 0
        root.numbers.forEach(function(n) { s += n })
        return s
    }

    function namedFunctionExpression(start: real): real {
        let s = start
        root.numbers.forEach(function add(n, i) {
            root.calls = i + 1
            s += n
        })
        return s
    }
}
