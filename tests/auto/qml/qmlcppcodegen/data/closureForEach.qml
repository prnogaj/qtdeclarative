pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property list<QtObject> objects: [
        QtObject { objectName: "a" },
        QtObject { objectName: "b" }
    ]
    property real last: 0

    function sum(): real {
        let s = 0
        root.numbers.forEach(n => { s += n })
        return s
    }

    function countAbove(limit: real): int {
        let count = 0
        const twice = limit * 2
        root.numbers.forEach((n, i) => {
            if (n <= twice)
                return
            root.last = n + i
            count++
        })
        return count
    }

    function sumOfIndices(): int {
        let s = 0
        root.numbers.forEach((n, i) => { s += i })
        return s
    }

    function names(): string {
        let result = ""
        root.objects.forEach(o => { result += o.objectName })
        return result
    }

    function noArguments(): int {
        let count = 0
        root.numbers.forEach(() => { ++count })
        return count
    }
}
