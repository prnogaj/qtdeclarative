import QtQml
import StructuredValues

QtObject {
    id: root

    property StructuredUser user: StructuredUser {}

    property myStructuredType complete: ({ number: 42, truth: false })
    property myStructuredType partial: ({ number: 42 })
    property myStructuredType empty: ({})
    property myOuterType nested: ({ label: "a", inner: { number: 1 } })

    // Not structured values: anything goes.
    property var anything: ({ number: 1, whatever: 2 })
    property var later

    function returned(): myStructuredType {
        return { truth: true }
    }

    function assigned(): void {
        root.user.value = { number: 1 }
        root.user.take({ truth: true })
        root.later = { number: 1, nonexistentProp: 2 }
        const local = { nonexistentProp: 1 }
        root.later = local
    }
}
