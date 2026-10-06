pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1, 2, 3, 4]
    property QtObject other: QtObject {
        id: inner
        property list<int> values: [10, 20, 30]
    }
    property string log

    // The callback changes an element it has not visited yet ...
    function changeAhead(): void {
        root.numbers.forEach((n, i) => {
            root.log += n + " "
            if (i == 0)
                root.numbers = [1, 2, 30, 40]
        })
    }

    // ... or removes elements: those are not visited ...
    function shrink(): void {
        root.numbers.forEach((n, i) => {
            root.log += n + " "
            if (i == 1)
                root.numbers = [1, 2]
        })
    }

    // ... or adds elements: forEach() has read the length before.
    function grow(): void {
        root.numbers.forEach(n => {
            root.log += n + " "
            if (root.numbers.length < 6)
                root.numbers.push(n * 100)
        })
    }

    // A for-of loop asks for the length at each step.
    function growForOf(): void {
        for (const n of root.numbers) {
            root.log += n + " "
            if (root.numbers.length < 6)
                root.numbers.push(n * 100)
        }
    }

    // A list that belongs to another object
    function otherObject(): void {
        inner.values.forEach((v, i) => {
            root.log += v + " "
            if (i == 0)
                inner.values = [10, 21]
        })
    }

    // A copy made before the loop is not affected.
    function copied(): void {
        const copy = root.numbers.slice()
        copy.forEach((n, i) => {
            root.log += n + " "
            if (i == 0)
                root.numbers = [9]
        })
    }
}
