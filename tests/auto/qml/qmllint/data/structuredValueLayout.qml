import QtQml
import StructuredValues

QtObject {
    id: root
    property StructuredUser user: StructuredUser {}

    // However the literal is laid out, the warnings point at the member or its value.
    // 1: one member per line
    property myStructuredType a: ({
        number: 42,
        truth: false,
        nonexistentProp: "Oops"
    })

    // 2: brace on its own line, odd spacing, trailing comma
    property myStructuredType b:
        ({
            number   :
                "42",
            wrongOne
                : 1,
        })

    // 3: nested over several lines
    property myOuterType c: ({
        label: "a",
        inner: {
            number: 1,
            thruth: true
        }
    })

    // 4: a value that spans lines
    property myStructuredType d: ({
        number: 1 +
                2.5,
        truth: "yes"
            + "no"
    })

    // 5: comments and strings that mention the names
    property myOuterType e: ({
        // label: this comment is not the member
        label: "inner: { number: 1 }",   /* number: nor this */
        inner: {
            number: "7"
        }
    })

    // 6: two literals in one function
    function f(): myStructuredType {
        root.user.value = {
            number: 1,
            first: true
        }
        return {
            number: 2,
            second: true
        }
    }

    // 7: quoted names, and a name that also appears in a string value before it
    property myStructuredType g: ({ "truth": "number: 1", 'number': "x", "wrong": 1 })

    // 8: the closing brace right after a value, no spaces at all
    property myStructuredType h: ({number:"1",truth:"x"})
}
