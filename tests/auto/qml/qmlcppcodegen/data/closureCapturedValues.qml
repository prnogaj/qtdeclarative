pragma Strict
import QtQml

QtObject {
    id: root

    property list<real> numbers: [1, 2, 3]
    property rect area: Qt.rect(1, 2, 3, 4)
    property real total: 0
    property string text
    property real factor: 2
    property var later

    // A value type created here, read in an inlined callback
    function sumWithOffset(): real {
        const offset = Qt.point(10, 20)
        let sum = 0
        root.numbers.forEach(n => { sum += n + offset.x + offset.y })
        return sum
    }

    // A value type and a list of values created here, read in a closure that escapes
    function makeReader(): var {
        const r = Qt.rect(root.area.x, 2, root.area.width, 4)
        const list = root.numbers.slice()
        return () => r.x + r.width + list.length + list[1]
    }

    // "this" in an arrow function is the object the function around it belongs to.
    function scaleAll(): real {
        let sum = 0
        root.numbers.forEach(n => { sum += n * this.factor })
        return sum
    }

    function installScaler(): void {
        root.later = (x: real): real => x * this.factor
    }

    // then() on a promise kept in a variable
    function chain(promise: var): void {
        const next = promise.then(v => { return v + 1 })
        next.then(v => { root.text = "got " + v })
    }

    // A change to a captured value type arrives in the variable, for an inlined callback ...
    function moved(): real {
        const p = Qt.point(1, 2)
        let sum = 0
        root.numbers.forEach(n => {
            p.x = p.x + n
            sum += p.x
        })
        return sum + p.x
    }

    // ... and for a closure that is called later.
    function makeMover(): var {
        const p = Qt.point(0, 10)
        return (dx: real): real => {
            p.x = p.x + dx
            return p.x + p.y
        }
    }

    // The same for a list of values
    function collect(): real {
        const out = root.numbers.slice()
        root.numbers.forEach(n => { out.push(n * 2) })
        let sum = 0
        for (let i = 0; i < out.length; ++i)
            sum += out[i] * 1
        return sum
    }
}
