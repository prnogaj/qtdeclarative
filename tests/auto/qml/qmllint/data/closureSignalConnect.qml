import QtQml

QtObject {
    id: root
    property string text
    property Timer timer: Timer { id: ticker }
    signal moved(x: real, name: string)

    function listen(): void {
        root.moved.connect((px, label) => { root.text = label.lenght + px.nope })
        root.moved.connect((px, label) => { root.text = label.length + px.toFixed(1) })
        ticker.triggered.connect(() => { root.text = ticker.intervall })
    }
}
