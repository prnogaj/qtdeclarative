import QtQml

// Closures we cannot compile as functions of their own. The functions that create them are
// not compiled either, and everything has to work as before.
QtObject {
    id: root

    // The parameter has no type.
    function untypedParameter(): var {
        let sum = 0
        return x => {
            sum += x
            return sum
        }
    }

    // Without a declared type, the closure can be called with anything.
    function concatenating(): var {
        let text = ""
        return x => {
            text += x
            return text
        }
    }

    function usesThis(): var {
        return function(): bool { return this === root }
    }
}
