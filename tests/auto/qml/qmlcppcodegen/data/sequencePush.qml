pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5]
    property list<string> names
    property list<int> counts: [1, 2]

    // The list is read from the property, changed, and written back.
    function addNumber(x: real): int {
        return root.numbers.push(x)
    }

    function addNames(a: string, b: string): void {
        root.names.push(a, b)
    }

    // A number of another type is converted to the type of the elements.
    function addCount(x: real): int {
        return root.counts.push(x)
    }

    function addInLoop(n: int): int {
        for (let i = 0; i < n; ++i)
            root.numbers.push(i)
        return root.numbers.length
    }
}
