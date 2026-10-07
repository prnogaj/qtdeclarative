import QtQml
import StructuredValues

QtObject {
    id: root
    property string text: "7"

    // QTBUG-139463: all of this is well-defined JavaScript, and "false" becomes true.
    property myStructuredType coerced: ({
        number: "42",
        truth: "false"
    })

    property myStructuredType fine: ({ number: 42.5, truth: true })
    property myOuterType nested: ({ label: 5, inner: { number: true, truth: 0 } })

    function fromProperty(): myStructuredType {
        return { number: root.text }
    }
}
