import QtQml
import ModuleWithStructuredValue

QtObject {
    id: root
    property StructuredUser user: StructuredUser {}
    property int count: 3

    property myStructuredType empty: ({ })
    property myStructuredType first: ({ n })
    property myStructuredType second: ({ number: 1, t })
    property myStructuredType value: ({ number: c })
    property myOuterType nested: ({ label: "a", inner: { tr } })
    property var anything: ({ x })

    function returned(): myStructuredType {
        return { tru }
    }
    function assigned(): void {
        root.user.value = { nu }
        root.user.take({ num })
    }
}
