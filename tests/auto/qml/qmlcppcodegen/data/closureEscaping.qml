pragma Strict
import QtQml
import TestTypes

QtObject {
    id: root

    property var callback
    property real total: 0
    property list<real> numbers: [1.5, 2.5, 4]
    property Person person: Person { name: "p"; shoeSize: 1 }

    // A function with a declared signature, stored in a property. It is called after this
    // function has returned, and it keeps its own "count".
    function install(step: real): void {
        let count = 0
        root.callback = function(x: real): real {
            count += x * step
            root.total = count
            return count
        }
    }

    // An arrow function without parameters, returned. Its return type is inferred.
    function makeCounter(start: int): var {
        let n = start
        return () => {
            n += 1
            return n
        }
    }

    // Captured variables of several types, one of them an object
    function makeDescriber(prefix: string): var {
        const who = root.person
        let calls = 0
        return () => {
            calls += 1
            who.shoeSize = who.shoeSize + 1
            return prefix + who.name + calls
        }
    }

    // A callback that is inlined and a closure that escapes, sharing a variable
    function sumThenAdder(): var {
        let sum = 0
        root.numbers.forEach(n => { sum += n })
        return function(x: real): real {
            sum += x
            return sum
        }
    }

    // A closure created by a closure
    function makeMaker(base: int): var {
        return function(factor: int): var {
            let calls = 0
            return () => {
                calls += 1
                return base * factor + calls
            }
        }
    }
}
