import QtQml

// Arrow functions whose declared types make them behave differently from what we would
// generate by inlining them. They coerce their arguments.
QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]

    // Each element is truncated to an integer on the way in.
    function sumAsIntegers(): real {
        let s = 0
        root.numbers.forEach((n: int) => { s += n })
        return s
    }

    // Each result is coerced to a string.
    function asStrings(): list<string> {
        return root.numbers.map((n: real): string => n * 2)
    }
}
