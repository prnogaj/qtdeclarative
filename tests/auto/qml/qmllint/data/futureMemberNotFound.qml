import QtQml
import Futures

QtObject {
    property FutureService service
    function load() {
        service.pending().finally(() => {})
    }
}
