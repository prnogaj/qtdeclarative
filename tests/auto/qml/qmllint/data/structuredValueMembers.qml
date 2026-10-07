import QtQml
import StructuredValues

QtObject {
    id: root

    property StructuredUser user: StructuredUser {}

    // QTBUG-139463
    property myStructuredType t: ({
        number: 42,
        truth: false,
        nonexistentProp: "Oops"
    })

    property myStructuredType quoted: ({ "number": 1, "nmber": 2 })

    property myOuterType nested: ({ label: "a", inner: { number: 1, thruth: true } })

    function returned(): myStructuredType {
        return { number: 1, extra: 2 }
    }

    function assigned(): void {
        root.user.value = { number: 1, wrong: true }
        root.user.take({ truth: true, alsoWrong: 1 })
    }
}
