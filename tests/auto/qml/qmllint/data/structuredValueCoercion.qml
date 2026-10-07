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

    // A number with a fraction loses it in an integer property. A whole number does not, and
    // of a number that is computed we cannot tell.
    property real factor: 1.5
    property myStructuredType fraction: ({ number: 8.5 })
    property myStructuredType whole: ({ number: 8.0 })
    property myStructuredType computed: ({ number: root.factor * 2 })
}
