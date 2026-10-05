pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property var callback

    // Parameters declared with the types the list has: inlined as before
    function sumTyped(): real {
        let s = 0
        root.numbers.forEach((n: real, i: int) => { s += n * i })
        return s
    }

    function doubledTyped(): list<real> {
        return root.numbers.map((n: real): real => n * 2)
    }

    function anyAboveTyped(limit: real): bool {
        return root.numbers.some((n: real): bool => n > limit)
    }

    // An arrow function with parameters that escapes. It needs the declared types, as anyone
    // can call it.
    function makeScaler(factor: real): var {
        return (x: real): real => x * factor
    }

    function installAdder(): void {
        let sum = 0
        root.callback = (x: real, y: int): real => {
            sum += x + y
            return sum
        }
    }
}
