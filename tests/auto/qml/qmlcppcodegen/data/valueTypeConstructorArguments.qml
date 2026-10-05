pragma Strict
import QtQml
import QtQuick
import QtQuick as QQ

QtObject {
    property int i: 5
    property real r: 2.5

    property vector2d v2: new QQ.vector2d(1, r)
    property vector3d v3: new QQ.vector3d(1, r, -3)
    property vector4d v4: new QQ.vector4d(1, r, -3, 4)
    property quaternion q: new QQ.quaternion(1, r, -3, 4)
    property matrix4x4 m: new QQ.matrix4x4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)

    property vector3d fromInts: new QQ.vector3d(i, i + 1, i + 2)
}
