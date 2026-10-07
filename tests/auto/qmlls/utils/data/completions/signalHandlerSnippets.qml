import QtQml

QtObject {
    id: root
    property string text
    signal moved(x: real, name: string)
    signal stopped()

    onMoved: (x, name) => { root.text = name + x }
    onStopped: s
    property Timer timer: Timer {
        onTriggered: root.text = "t"
    }
    onTextChanged: t
}
