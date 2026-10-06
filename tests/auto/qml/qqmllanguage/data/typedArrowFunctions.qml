pragma FunctionSignatureBehavior: Enforced

import QtQml

QtObject {
    // The arguments are coerced to the declared types, and so is what is returned.
    property var toInt: (x: int) => x
    property var lengthOf: (s: string): int => s.length
    property var half: (x: real): int => x / 2
    property var constant: (): int => 42.7
    property var untyped: (x) => x

    property var a: toInt("12")
    property var b: lengthOf(12345)
    property var c: half(9)
    property var d: constant()
    property var e: untyped("12")

    // Passed on as a callback, with the types the caller happens to pass
    property list<int> numbers: [1, 2, 3]
    property var f: numbers.map((n: string): string => n + "!").join("")
}
