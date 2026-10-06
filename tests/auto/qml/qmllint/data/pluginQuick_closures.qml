import QtQml
import QtQuick.Controls

QtObject {
    id: root
    property list<real> sizes: [1, 2, 3]
    property string text

    // An attached property that needs an Item, read where there is none: in a callback that
    // is linted with its caller, in a loop, and in a closure nobody calls here.
    function inCallback(): void {
        root.sizes.forEach(size => { root.text = ToolTip.text + size })
    }

    function inLoop(): void {
        for (let i = 0; i < root.sizes.length; ++i)
            root.sizes.forEach(size => { root.text = ToolTip.text + size + i })
    }

    function inClosure(): var {
        let count = 0
        return () => { count += 1; return ToolTip.text + count }
    }
}
