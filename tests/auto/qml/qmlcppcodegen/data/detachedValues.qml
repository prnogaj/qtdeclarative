pragma Strict
import QtQml

QtObject {
    id: root

    property int counter: 0

    // Arguments are copies. Writing a property of some object does not change them.
    function rectAfterSideEffect(a: rect): real {
        root.counter = root.counter + 1
        return a.x + a.width
    }

    function listAfterSideEffect(numbers: list<real>): real {
        let sum = 0
        for (let i = 0; i < numbers.length; ++i) {
            root.counter = root.counter + 1
            sum += numbers[i]
        }
        return sum
    }

    // The same for a part of an argument held in a local ...
    function partAfterSideEffect(a: rect): real {
        const width = a.width
        const numbers = [width]
        root.counter = root.counter + 1
        return a.height + numbers.length
    }

    // ... and for a value created here.
    function createdAfterSideEffect(x: real): real {
        const a = Qt.rect(x, 2, 3, 4)
        root.counter = root.counter + 1
        return a.x + a.height
    }
}
