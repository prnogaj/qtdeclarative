import QtQuick as Q

Q.QtObject {
    property var vector2d: new Q.vector2d(1, 2.5)
    property var vector3d: new Q.vector3d(1, 2.5, -3)
    property var vector4d: new Q.vector4d(1, 2.5, -3, 4)
    property var quaternion: new Q.quaternion(1, 2.5, -3, 4)
    property var matrix4x4: new Q.matrix4x4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)

    // The constructors taking one argument are still selected for one argument.
    property var fromObject: new Q.vector3d({x: 4, y: 5, z: 6})
    property var fromValue: new Q.vector3d(Qt.vector3d(7, 8, 9))
}
