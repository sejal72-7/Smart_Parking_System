#include "auth.hpp"
#include "dsa.hpp"

#include <cassert>
#include <iostream>
#include <vector>

static void testMinHeap() {
    dsa::SlotMinHeap heap;
    assert(heap.isEmpty());

    heap.insert(5);
    heap.insert(2);
    heap.insert(9);
    heap.insert(1);
    heap.insert(4);
    heap.insert(2); // duplicate ignored

    assert(heap.size() == 5);

    int minSlot = 0;
    assert(heap.peekMin(minSlot) && minSlot == 1);

    // Extract in ascending order: 1, 2, 4, 5, 9
    std::vector<int> extracted;
    while (heap.extractMin(minSlot)) {
        extracted.push_back(minSlot);
    }
    assert((extracted == std::vector<int>{1, 2, 4, 5, 9}));
    assert(heap.isEmpty());

    // Remove specific slot (manual selection)
    heap.rebuild({1, 2, 3, 4, 5});
    assert(heap.remove(1));
    assert(heap.peekMin(minSlot) && minSlot == 2);
    assert(heap.remove(4));
    assert(heap.size() == 3);

    std::cout << "[PASS] SlotMinHeap\n";
}

static void testWaitingQueue() {
    dsa::WaitingQueue q;
    assert(q.isEmpty());

    q.enqueue({101, "Alice", "OD02AB1111", "Car", "2026-01-01 10:00:00"});
    q.enqueue({102, "Bob", "OD02CD2222", "Bike", "2026-01-01 10:01:00"});
    q.enqueue({103, "Charlie", "OD02EF3333", "EV", "2026-01-01 10:02:00"});

    assert(q.size() == 3);
    assert(q.positionOfUser(101) == 1);
    assert(q.positionOfUser(102) == 2);
    assert(q.positionOfUser(103) == 3);

    assert(q.removeByUser(102));
    assert(q.size() == 2);
    assert(q.positionOfUser(103) == 2);

    dsa::WaitlistEntry out;
    assert(q.dequeue(out) && out.userId == 101 && out.regNumber == "OD02AB1111");
    assert(q.dequeue(out) && out.userId == 103 && out.regNumber == "OD02EF3333");
    assert(q.isEmpty());

    std::cout << "[PASS] WaitingQueue (FIFO)\n";
}

static void testVehicleHashMap() {
    dsa::VehicleHashMap map;
    assert(map.size() == 0);

    map.insert({1, 10, "Alice", "alice@test.com", "od-02 ab-1234", "Car", 3});
    map.insert({2, 11, "Bob", "bob@test.com", "KA01XY9999", "EV", 0});

    assert(map.size() == 2);

    dsa::VehicleRecord found;
    assert(map.find("OD02AB1234", found));
    assert(found.ownerName == "Alice");
    assert(found.activeSlotNumber == 3);

    assert(map.find("ka01 xy 9999", found));
    assert(found.ownerName == "Bob");

    assert(!map.find("MH12ZZ0000", found));
    assert(map.remove("OD02AB1234"));
    assert(map.size() == 1);
    assert(!map.find("OD02AB1234", found));

    std::cout << "[PASS] VehicleHashMap\n";
}

static void testOperationStack() {
    dsa::OperationStack stack;
    assert(stack.isEmpty());

    stack.push({"ENTRY", 1, "P-01", "OD02AB1234", "Alice", "2026-01-01 09:00:00"});
    stack.push({"ENTRY", 2, "P-02", "KA01XY9999", "Bob", "2026-01-01 09:05:00"});
    stack.push({"EXIT", 1, "P-01", "OD02AB1234", "Alice", "2026-01-01 10:00:00"});

    auto recent = stack.getRecent(10);
    assert(recent.size() == 3);
    assert(recent[0].action == "EXIT" && recent[0].slotNumber == 1);
    assert(recent[1].action == "ENTRY" && recent[1].slotNumber == 2);
    assert(recent[2].action == "ENTRY" && recent[2].slotNumber == 1);

    dsa::ParkingOperation top;
    assert(stack.pop(top) && top.action == "EXIT");
    assert(stack.size() == 2);

    std::cout << "[PASS] OperationStack (LIFO)\n";
}

static void testAuthHashingAndSessions() {
    std::string salt = auth::randomHex(16);
    std::string h1 = auth::hashPassword("mySecret123", salt);
    std::string h2 = auth::hashPassword("mySecret123", salt);
    std::string h3 = auth::hashPassword("wrongSecret", salt);
    assert(h1.size() == 64);
    assert(h1 == h2);
    assert(h1 != h3);

    auth::SessionManager sm;
    std::string token = sm.createSession(7, "Test User", "test@example.com", "user");
    auth::SessionUser su;
    assert(sm.validate(token, su));
    assert(su.userId == 7 && su.role == "user");
    sm.remove(token);
    assert(!sm.validate(token, su));

    std::cout << "[PASS] Auth SHA-256 & SessionManager\n";
}

int main() {
    testMinHeap();
    testWaitingQueue();
    testVehicleHashMap();
    testOperationStack();
    testAuthHashingAndSessions();
    std::cout << "All Smart Parking C++ DSA & Auth tests passed!\n";
    return 0;
}

