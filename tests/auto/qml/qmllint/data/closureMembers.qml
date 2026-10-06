import QtQml

QtObject {
    id: root

    property list<QtObject> objects
    property list<real> numbers
    property string text
    property var stored

    function inCallbacks(): void {
        root.objects.forEach(o => { root.text = o.objectNam })
        root.numbers.forEach((n, i) => { root.text = n.nope + i.neither })
        const first = root.objects.filter(o => o.objectName.lenght > 0)
        root.objects.forEach(o => {
            root.numbers.forEach(n => { root.text = o.inner + n })
        })
    }

    function inOtherClosures(): void {
        const count = root.numbers.length
        root.stored = () => { root.text = root.missing + count.wrong }
        root.stored = (o: QtObject) => o.typo
    }

    function withAllArguments(): void {
        root.numbers.forEach((n, i, all) => { root.text = all.lenght + n.wrong })
    }
}
