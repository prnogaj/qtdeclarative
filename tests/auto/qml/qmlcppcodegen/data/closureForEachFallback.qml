import QtQml

// Callbacks we cannot inline. These functions have to give the right results anyway.
QtObject {
    id: root

    property list<real> numbers: [1.5, 2.5, 4]
    property list<QtObject> objects: [ QtObject {}, QtObject {} ]

    // The outer callback needs a context of its own for "n".
    function nested(): real {
        let s = 0
        root.numbers.forEach(n => {
            root.objects.forEach(o => { s += n })
        })
        return s
    }

    function withArray(): real {
        let s = 0
        root.numbers.forEach((n, i, array) => { s += array[i] })
        return s
    }

    function functionExpression(): real {
        let s = 0
        root.numbers.forEach(function(n) { s += n })
        return s
    }

    function storedCallback(): real {
        let s = 0
        const callback = n => { s += n }
        root.numbers.forEach(callback)
        callback(10)
        return s
    }

    function capturingCallback(): real {
        let s = 0
        root.numbers.forEach(n => {
            const add = () => { s += n }
            add()
        })
        return s
    }
}
