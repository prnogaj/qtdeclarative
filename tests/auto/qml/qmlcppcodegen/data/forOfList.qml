pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5, -1, 2.5, 4]
    property list<QtObject> objects: [
        QtObject { objectName: "a" },
        QtObject { objectName: "b" }
    ]
    property real last: 0
    property string names

    // break and continue
    function sumUpTo(limit: real): real {
        let s = 0
        for (const n of root.numbers) {
            if (n > limit)
                break
            if (n < 0)
                continue
            s += n
        }
        return s
    }

    // A list passed as argument, with a side effect in the loop
    function sumOfArgument(values: list<real>): real {
        let s = 0
        for (const v of values) {
            root.last = v
            s += v
        }
        return s
    }

    function collectNames(): void {
        let result = ""
        for (const o of root.objects)
            result += o.objectName
        root.names = result
    }

    // The loop is skipped by a jump, and left by a return.
    function firstAbove(limit: real, enabled: bool): real {
        if (enabled) {
            for (const n of root.numbers) {
                if (n > limit)
                    return n
            }
        }
        return -1
    }

    function nested(): real {
        let s = 0
        for (const n of root.numbers) {
            for (const m of root.numbers)
                s += n * m
        }
        return s
    }

    function throwing(): real {
        let s = 0
        for (const n of root.numbers) {
            if (n < 0)
                throw "negative"
            s += n
        }
        return s
    }

    function firstTwo(): real {
        const [a, b] = root.numbers
        return a + b
    }
}
