pragma Strict
import QtQml
import TestTypes

Collector {
    id: self

    property Component c: QtObject { objectName: "dynamic" }
    property list<int> numbers: [1, 2, 3]

    // Pass the objects through properties once so that we know they have wrappers.
    property QtObject hiddenOuter: c.createObject()
    property QtObject hiddenInner: c.createObject()

    property QtObject keptOuter
    property QtObject keptInner
    property int gcRuns: 0

    function run(): void {
        // From here on, only a register of this function refers to the object.
        var outerObject = self.hiddenOuter
        self.hiddenOuter = null

        let runs = 0
        self.numbers.forEach(n => {
            // The same for a register of the callback.
            var innerObject = self.hiddenInner
            self.hiddenInner = null

            // Hide this behind a property so that the side effect can't be detected.
            runs += self.gc

            if (n == 1)
                self.keptInner = innerObject
        })

        // Once more, when the registers of this function are the tracked locals again.
        runs += self.gc

        self.gcRuns = runs
        self.keptOuter = outerObject
    }
}
