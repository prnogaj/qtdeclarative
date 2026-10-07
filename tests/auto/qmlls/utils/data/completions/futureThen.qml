import QtQml
import ModuleWithFuture

QtObject {
    id: root
    property Service service: Service {}
    property int size

    function load(): void {
        root.service.fetchPerson().then(person => { root.size = person.shoeSize })
        root.service.fetchPersons().then(persons => {
            persons.forEach(each => { root.size += each.shoeSize })
        })
        root.service.fetchPerson().then(ok => {}, failure => { root.size = failure.shoeSize })
        root.service.wait().then(nothing => { root.size = nothing.shoeSize })
        root.service.fetchPerson().then(p => {})
    }
}
