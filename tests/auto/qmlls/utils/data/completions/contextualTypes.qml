import QtQml

QtObject {
    id: root
    property list<QtObject> objects
    property list<Timer> timers
    property string text

    function untyped(): void {
        root.objects.forEach(o => { root.text = o.objectName })
        const first = root.objects[0]
        root.text = first.objectName
        root.timers.filter(t => t.running).forEach(u => { root.text = u.interval })
        const timer = root.timers[1]
        const same = timer
        root.text = same.interval
        root.objects.forEach(function(p) { root.text = p.objectName })
        root.timers.forEach((v, index) => { root.text = v.repeat })
        root.timers.forEach((w: QtObject) => { root.text = w.objectName })
        let unknown = someFunction()
        root.text = unknown.objectName
    }
}
