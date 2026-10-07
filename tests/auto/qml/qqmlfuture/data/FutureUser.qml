import QtQml
import FutureTest

// Compiled by qmlcachegen, so that the futures also pass through AOT-compiled code.
QtObject {
    id: root

    required property Service service
    property var request
    property var fromBinding: service.current
    property int value: -1

    function store() { request = service.pending() }
    function thenDirect() { service.pending().then(result => root.value = result) }
    function thenStored() { request.then(result => root.value = result) }
    function thenFromBinding() { fromBinding.then(result => root.value = result) }
    function thenProperty() { service.current.then(result => root.value = result) }
    function passStored() { service.take(request) }
    function passDirect() { service.take(service.pending()) }
    function passProperty() { service.take(service.current) }

    // A callback that declares what it gets
    property string names
    function typedCallback() {
        service.people().then((people: list<Person>): void => {
            let all = ""
            people.forEach(person => { all += person.name })
            root.names = people.length + all
        })
    }

    function typedStore() { request = Backend.pending() }
    function typedThen() { Backend.pending().then(result => root.value = result) }
    function typedPassDirect() { Backend.take(Backend.pending()) }
    function typedPassProperty() { Backend.take(Backend.current) }
    function typedPassStored() { Backend.take(request) }
}
