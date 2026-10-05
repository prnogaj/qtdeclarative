import QtQuick as Q

Q.QtObject {
    id: root

    // No invokable toString(). The prototype has to provide it.
    property string constructedMatrix: String(new Q.matrix4x4())
    property string createdMatrix: String(Qt.matrix4x4())
    property string constructedEasingCurve: String(new Q.easingCurve())

    property bool samePrototype: Object.getPrototypeOf(new Q.vector3d())
                                 === Object.getPrototypeOf(Qt.vector3d(0, 0, 0))
    property bool isInstance: new Q.vector3d() instanceof Q.vector3d

    function constructorIsType(): bool {
        const T = Q.vector3d
        return (new T()).constructor === T
    }
    property bool hasConstructor: constructorIsType()

    function objectPrototypeIsType(): bool {
        const T = Q.QtObject
        return Object.getPrototypeOf(new T(root)) === T
    }
    property bool objectPrototype: objectPrototypeIsType()
}
