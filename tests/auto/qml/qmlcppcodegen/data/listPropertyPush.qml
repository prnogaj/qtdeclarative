pragma Strict
import QtQml

QtObject {
    id: root

    property list<QtObject> objects
    property QtObject a: QtObject { objectName: "a" }
    property QtObject b: QtObject { objectName: "b" }

    property int lengthAfterOne: -1
    property int lengthAfterThree: -1

    function pushOne(o: QtObject): int {
        return root.objects.push(o)
    }

    function pushTwo(o: QtObject, p: QtObject): void {
        root.objects.push(o, p)
    }

    Component.onCompleted: {
        lengthAfterOne = pushOne(a)
        pushTwo(b, null)
        lengthAfterThree = objects.length
    }
}
