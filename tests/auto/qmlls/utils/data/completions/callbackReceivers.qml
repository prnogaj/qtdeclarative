import QtQml
import ModuleWithFuture

QtObject {
    id: root
    property Service service: Service {}
    property var anything
    property string text
    signal moved(x: real, name: string)

    onMoved: m

    function f(): void {
        root.moved.connect(s)
        root.moved.connect((px, label) => { root.text = label.length })
        root.service.fetchPerson().then(a)
        root.service.fetchPerson().then((person: Person): int => person.shoeSize).then(b)
        root.service.fetchPerson().then(p => p.shoeSize).catch(e)
        root.service.fetchPerson().then(p => p.shoeSize).finally(z)
        root.service.fetchPerson().then((person: Person): int => person.shoeSize).then(n => n.toFixed(1))
        root.service.fetchPerson().then(p => p.shoeSize).th
        root.anything.forEach(u)
        root.service.forEach(w)
    }
}
