pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property list<QtObject> objects: [
        QtObject { objectName: "a" },
        QtObject { objectName: "bb" },
        QtObject { objectName: "c" }
    ]
    property int calls: 0

    function someAbove(limit: real): bool {
        return root.numbers.some(n => n > limit)
    }

    function everyAbove(limit: real): bool {
        return root.numbers.every(n => n > limit)
    }

    // The iteration stops at the first element that decides the result.
    function someCounting(limit: real): bool {
        let count = 0
        const result = root.numbers.some(n => {
            ++count
            return n > limit
        })
        root.calls = count
        return result
    }

    function indexOfFirstAbove(limit: real): int {
        return root.numbers.findIndex(n => n > limit)
    }

    function above(limit: real): list<real> {
        return root.numbers.filter(n => n > limit)
    }

    function evenIndices(): list<real> {
        return root.numbers.filter((n, i) => i % 2 == 0)
    }

    function shortNames(): string {
        const short = root.objects.filter((o, i) => i != 1)
        return short.length + ": " + short.map(o => o.objectName).join("+")
    }

    function doubled(): list<real> {
        return root.numbers.map(n => n * 2)
    }

    function scaled(factor: real): list<real> {
        return root.numbers.map((n, i) => n * factor + i)
    }

    function names(): list<string> {
        return root.objects.map(o => o.objectName)
    }

    // A callback that does not return on all paths returns undefined, which is false.
    function implicitUndefined(): int {
        return root.numbers.findIndex(n => {
            if (n > 2)
                return true
        })
    }
}
