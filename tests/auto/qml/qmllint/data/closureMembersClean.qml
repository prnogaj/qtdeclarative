import QtQml

QtObject {
    id: root

    property list<QtObject> objects
    property list<real> numbers
    property string text
    property var stored
    property real total

    function inCallbacks(): void {
        let sum = 0
        let names = ""
        root.objects.forEach(o => { names += o.objectName })
        root.numbers.forEach((n, i) => { sum += n * i })
        const named = root.objects.filter(o => o.objectName.length > 0)
        const doubled = root.numbers.map(n => n * 2)
        const any = root.numbers.some(n => n > sum)
        root.objects.forEach((o, i, all) => { names += all.length })
        root.total = sum + named.length + doubled.length + (any ? 1 : 0)
        root.text = names
    }

    function inOtherClosures(): void {
        let calls = 0
        root.stored = (x) => { calls += 1; root.total = x + calls }
        root.stored = (o: QtObject): string => o.objectName
        const later = function(value) { return value.anything }
        root.text = later({ anything: "a" })
    }

    // What filter() returns is a list of the same elements.
    property list<Timer> timers
    function running(): list<Timer> {
        return root.timers.filter(timer => timer.running)
    }
    function intervals(): void {
        const running = root.timers.filter((timer: Timer): bool => timer.running)
        running.forEach(timer => { root.total += timer.interval })
    }
}
