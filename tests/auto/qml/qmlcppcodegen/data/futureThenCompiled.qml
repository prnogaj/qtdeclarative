pragma Strict
import QtQml
import TestTypes

QtObject {
    id: root

    property FutureProvider provider: FutureProvider {}

    property int result: -1
    property string names
    property int sizes: 0
    property int calls: 0
    property string outcome

    // The callback gets the result of the future, with the declared type.
    function fetchNumber(value: int): void {
        root.provider.number(value).then((n: int): void => {
            root.result = n + 1
        })
    }

    // A list of objects, and a loop over it in the callback. The loop's callback is inlined
    // into the then() callback. "calls" is captured by the then() callback from this function.
    function fetchPeople(): void {
        let calls = root.calls
        root.provider.people().then((people: list<Person>): void => {
            let names = ""
            let sizes = 0
            people.forEach(p => {
                names += p.name
                sizes += p.shoeSize
            })
            calls += 1
            root.names = names
            root.sizes = sizes
            root.calls = calls
        })
    }

    // then() returns a promise. catch() on that is a call of the same kind.
    function fetchCanceled(): void {
        root.provider.canceled().then((n: int): void => {
            root.outcome = "fulfilled"
        }).catch((error: var): void => {
            root.outcome = "rejected"
        })
    }

    // Without type annotations: the future tells us what the callback gets.
    function fetchNumberUntyped(value: int): void {
        root.provider.number(value).then(n => {
            root.result = n + 2
        })
    }

    function fetchPeopleUntyped(): void {
        root.provider.people().then(people => {
            let names = ""
            let sizes = 0
            for (const p of people) {
                names += p.name
                sizes += p.shoeSize
            }
            root.names = names + "!"
            root.sizes = sizes + 1
        })
    }

    function fetchCanceledUntyped(): void {
        root.provider.canceled().then(n => {
            root.outcome = "fulfilled " + n
        }, error => {
            root.outcome = "rejected untyped"
        })
    }

    function fetchCaughtUntyped(): void {
        root.provider.canceled().catch(error => {
            root.outcome = "caught untyped"
        })
    }

    // then() and catch() on what then() returns
    property string chained
    function fetchChained(value: int): void {
        root.provider.number(value).then(n => {
            return n * 2
        }).then(doubled => {
            root.chained = "got " + doubled
        }).catch(error => {
            root.chained = "failed"
        })
    }

    function fetchChainedCanceled(): void {
        root.provider.canceled().then(n => {
            root.chained = "fulfilled"
        }).catch(error => {
            root.chained = "caught"
        })
    }
}
