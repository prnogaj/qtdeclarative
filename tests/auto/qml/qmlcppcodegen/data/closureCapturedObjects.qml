pragma Strict
import QtQml
import TestTypes

QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property list<Person> people: [
        Person { name: "a"; shoeSize: 1 },
        Person { name: "b"; shoeSize: 2 }
    ]
    property Person target: Person { name: "target" }

    // An object in a variable of the function, used in the callback
    function writeThroughCaptured(): int {
        const person = root.target
        let count = 0
        root.numbers.forEach(n => {
            person.shoeSize = person.shoeSize + 10
            ++count
        })
        return person.shoeSize + count
    }

    // The parameter of a callback, an object, captured by the callback inside it
    function namesTimesNumbers(): string {
        let result = ""
        root.people.forEach(p => {
            root.numbers.forEach(n => { result += p.name })
        })
        return result
    }

    // A captured variable that is assigned another object in the callback
    function lastPerson(): string {
        let last = root.target
        root.people.forEach(p => { last = p })
        return last.name
    }

    // A list of objects in a variable, used in the callback
    function sizesViaCapturedList(): int {
        const list = root.people
        let sum = 0
        root.numbers.forEach((n, i) => {
            if (i < list.length)
                sum += list[i].shoeSize
        })
        return sum
    }

    // An object argument of the function itself, captured
    function capturedArgument(person: Person): string {
        let result = ""
        root.numbers.forEach(n => { result += person.name })
        return result
    }
}
