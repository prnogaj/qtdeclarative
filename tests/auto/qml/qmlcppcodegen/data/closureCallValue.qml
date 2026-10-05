pragma Strict
import QtQml

QtObject {
    id: root

    property var stored: (x: real): real => x + 100
    property string log

    // A function passed as argument
    function apply(f: var, x: real): real {
        return f(x)
    }

    // A function kept in a variable and called several times
    function callLocal(x: real): real {
        const twice = (v: real): real => v * 2
        return twice(x) + twice(1)
    }

    // A function taken from a property, and one that returns nothing
    function callStored(x: real): real {
        const f = root.stored
        const note = (text: string): void => { root.log += text }
        note("a")
        note("b")
        return f(x)
    }

    // The callback is called in a loop, and captures a variable of the caller
    function sumWith(f: var, count: int): real {
        let sum = 0
        for (let i = 0; i < count; ++i)
            sum += f(i)
        return sum
    }

    function callNothing(f: var): void {
        f()
    }
}
