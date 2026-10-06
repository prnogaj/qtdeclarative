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
}
