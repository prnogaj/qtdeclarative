import QtQml

QtObject {
    property var one: (x: real) => x
    property var two: (x: real, i: int) => x * i
    property var withReturnType: (x: real, i: int): real => x * i
    property var withBody: (name: string): string => {
        return name + "!"
    }
    property var listParameter: (numbers: list<real>): int => numbers.length
    property var nested: (x: int): var => (y: int): int => x + y

    function callbacks(numbers: list<real>): real {
        let sum = 0
        numbers.forEach((n: real, i: int) => { sum += n * i })
        return numbers.some((n: real): bool => n > sum) ? sum : -sum
    }
}
