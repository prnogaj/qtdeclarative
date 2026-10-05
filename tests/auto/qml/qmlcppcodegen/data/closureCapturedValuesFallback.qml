import QtQml

QtObject {
    id: root

    property list<real> numbers: [1, 2, 3]
    property rect area: Qt.rect(1, 2, 3, 4)

    // A value type read from a property stays a reference to that property when a closure
    // keeps it. We don't compile this.
    function makeLiveReader(): var {
        const r = root.area
        return () => r.x + r.width
    }

    function makeLiveLength(): var {
        const list = root.numbers
        return () => list.length
    }

    // A change to a captured value would be made to a copy.
    function moved(): real {
        const p = Qt.point(1, 2)
        let sum = 0
        root.numbers.forEach(n => {
            p.x = p.x + n
            sum += p.x
        })
        return sum
    }
}
