import QtQml
import Futures

QtObject {
    id: root
    property FutureService service
    property var request
    property int value

    function load() {
        request = service.pending()
        request.then(result => root.value = result)
        service.pending().then(result => root.value = result, error => console.warn(error))
        service.pending().then(result => root.value = result).catch(error => console.warn(error))
        service.current.catch(error => console.warn(error))
        service.take(service.pending())
    }
}
