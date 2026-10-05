pragma Strict
import QtQuick

QtObject {
    id: root

    property vector2d v2: Qt.vector2d(1, 2)
    property vector3d v3: Qt.vector3d(1, 2, 3)
    property vector4d v4: Qt.vector4d(1, 2, 3, 4)
    property quaternion q: Qt.quaternion(1, 2, 3, 4)
    property matrix4x4 m: Qt.matrix4x4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)

    // The result is known to be a vector3d. Its properties can be read ...
    function sumOfComponents(x: real): real {
        const v = Qt.vector3d(x, 2, 3)
        return v.x + v.y + v.z
    }

    // ... and it can be returned as one.
    function make(x: real, y: int): vector3d {
        return Qt.vector3d(x, y, 0.5)
    }
}
