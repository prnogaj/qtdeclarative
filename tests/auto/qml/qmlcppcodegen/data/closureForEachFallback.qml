import QtQml

// Callbacks we cannot inline. These functions have to give the right results anyway.
QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    function withArray(): real {
        let s = 0
        root.numbers.forEach((n, i, array) => { s += array[i] })
        return s
    }

    // A function expression has its own "this". It is not the object the outer function
    // belongs to.
    function functionWithThis(): bool {
        let outerThis = false
        root.numbers.forEach(function(n) { outerThis = (this === root) })
        return outerThis
    }

    function functionWithArguments(): int {
        let count = 0
        root.numbers.forEach(function(n) { count += arguments.length })
        return count
    }

    // An object captured by a closure cannot be held in a C++ variable.
    function capturedObject(): string {
        let names = ""
        const objects = [root, root]
        root.numbers.forEach(n => {
            objects.forEach(o => { names += "x" })
        })
        return names
    }

    function storedCallback(): real {
        let s = 0
        const callback = n => { s += n }
        root.numbers.forEach(callback)
        callback(10)
        return s
    }
}
