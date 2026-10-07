import QtQuick

Item {
    property real a: Qt.vector3d(1, 2, 3).nope
    property real b: Qt.vector3d(1, 2, 3).x
    property vector3d c: Qt.vector3d(1, 2, 3).times(2)
    property real d: Qt.vector2d(1, 2).lenght()
    property real e: Qt.quaternion(1, 0, 0, 0).scalar
}
