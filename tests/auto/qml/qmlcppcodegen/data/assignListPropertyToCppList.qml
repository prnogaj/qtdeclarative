pragma Strict
import QtQml
import TestTypes

QtObject {
    id: root

    property list<Person> people: [
        Person { name: "a" },
        Person { name: "b" }
    ]
    property list<Person> others: [
        Person { name: "c" }
    ]

    // A list property implemented in C++, without a setter.
    property BirthdayParty party: BirthdayParty {}

    function assignPeople(): void {
        root.party.guests = root.people
    }

    function assignOthers(): void {
        root.party.guests = root.others
    }
}
