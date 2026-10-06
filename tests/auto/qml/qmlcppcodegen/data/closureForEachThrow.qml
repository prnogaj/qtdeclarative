pragma Strict
import QtQml

QtObject {
    id: root

    property list<int> numbers: [1, 2, 3]
    property int visited: 0
    property int completed: 0
    property bool after: false

    function run(): void {
        let count = 0
        root.numbers.forEach(n => {
            root.visited = n
            if (n == 2)
                throw "ouch"
            count++
            root.completed = count
        })
        root.after = true
    }
}
