import QtQml

QtObject {
    id: root
    property list<QtObject> objects
    property list<real> numbers
    property var stored
    property string text

    function f(): void {
        root.objects.forEach(o)
        root.numbers.filter(n)
        root.numbers.map(m)
        root.numbers.reduce(r)
        root.numbers.sort(c)
        root.stored = s
        root.objects.forEach((one, index, all) => { root.text = all.length })
        root.numbers.sort((x, y) => x.toFixed(1) - y)
    }
}
