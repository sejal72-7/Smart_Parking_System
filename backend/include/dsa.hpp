#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dsa {

// Helper: get current local timestamp as "YYYY-MM-DD HH:MM:SS"
inline std::string currentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#ifdef _WIN32
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmNow, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Helper: normalize vehicle registration number (uppercase, remove spaces/hyphens)
inline std::string normalizeReg(const std::string& reg) {
    std::string out;
    out.reserve(reg.size());
    for (unsigned char ch : reg) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return out;
}

// ============================================================================
// 1. QUEUE (FIFO) - Waiting List when all parking slots are occupied
//    Implemented as a Singly Linked List with front and rear pointers:
//    enqueue() -> O(1), dequeue() -> O(1), peek() -> O(1)
// ============================================================================
struct WaitlistEntry {
    int userId = 0;
    std::string fullName;
    std::string regNumber;
    std::string vehicleType;
    std::string requestedAt;
};

class WaitingQueue {
private:
    struct Node {
        WaitlistEntry data;
        Node* next;
        explicit Node(const WaitlistEntry& entry) : data(entry), next(nullptr) {}
    };

    Node* front_ = nullptr;
    Node* rear_ = nullptr;
    size_t count_ = 0;

public:
    WaitingQueue() = default;

    ~WaitingQueue() { clear(); }

    WaitingQueue(const WaitingQueue&) = delete;
    WaitingQueue& operator=(const WaitingQueue&) = delete;

    bool isEmpty() const { return count_ == 0; }
    size_t size() const { return count_; }

    // Add a user to the rear of the waiting list (FIFO)
    void enqueue(const WaitlistEntry& entry) {
        Node* node = new Node(entry);
        if (rear_ == nullptr) {
            front_ = rear_ = node;
        } else {
            rear_->next = node;
            rear_ = node;
        }
        ++count_;
    }

    // Remove and return the user at the front of the waiting list
    bool dequeue(WaitlistEntry& out) {
        if (isEmpty()) return false;
        Node* temp = front_;
        out = temp->data;
        front_ = front_->next;
        if (front_ == nullptr) {
            rear_ = nullptr;
        }
        delete temp;
        --count_;
        return true;
    }

    bool peek(WaitlistEntry& out) const {
        if (isEmpty()) return false;
        out = front_->data;
        return true;
    }

    bool containsUser(int userId) const {
        for (Node* curr = front_; curr != nullptr; curr = curr->next) {
            if (curr->data.userId == userId) return true;
        }
        return false;
    }

    // Returns 1-based position in queue, or 0 if not in queue
    int positionOfUser(int userId) const {
        int pos = 1;
        for (Node* curr = front_; curr != nullptr; curr = curr->next) {
            if (curr->data.userId == userId) return pos;
            ++pos;
        }
        return 0;
    }

    bool removeByUser(int userId) {
        Node* prev = nullptr;
        Node* curr = front_;
        while (curr != nullptr) {
            if (curr->data.userId == userId) {
                if (prev == nullptr) {
                    front_ = curr->next;
                } else {
                    prev->next = curr->next;
                }
                if (curr == rear_) {
                    rear_ = prev;
                }
                delete curr;
                --count_;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    std::vector<WaitlistEntry> toList() const {
        std::vector<WaitlistEntry> result;
        result.reserve(count_);
        for (Node* curr = front_; curr != nullptr; curr = curr->next) {
            result.push_back(curr->data);
        }
        return result;
    }

    void clear() {
        while (front_ != nullptr) {
            Node* temp = front_;
            front_ = front_->next;
            delete temp;
        }
        rear_ = nullptr;
        count_ = 0;
    }
};

// ============================================================================
// 2. MIN HEAP (Priority Queue) - Auto-allocate lowest-numbered available slot
//    Binary Min Heap backed by std::vector<int>:
//    insert() -> O(log n), extractMin() -> O(log n), peekMin() -> O(1)
// ============================================================================
class SlotMinHeap {
private:
    std::vector<int> heap_;

    void heapifyUp(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (heap_[index] < heap_[parent]) {
                std::swap(heap_[index], heap_[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    void heapifyDown(size_t index) {
        size_t n = heap_.size();
        while (true) {
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;
            size_t smallest = index;

            if (left < n && heap_[left] < heap_[smallest]) {
                smallest = left;
            }
            if (right < n && heap_[right] < heap_[smallest]) {
                smallest = right;
            }
            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }

public:
    bool isEmpty() const { return heap_.empty(); }
    size_t size() const { return heap_.size(); }

    void insert(int slotNumber) {
        // Avoid duplicate slot numbers in the heap
        for (int val : heap_) {
            if (val == slotNumber) return;
        }
        heap_.push_back(slotNumber);
        heapifyUp(heap_.size() - 1);
    }

    bool peekMin(int& outSlotNumber) const {
        if (heap_.empty()) return false;
        outSlotNumber = heap_[0];
        return true;
    }

    bool extractMin(int& outSlotNumber) {
        if (heap_.empty()) return false;
        outSlotNumber = heap_[0];
        heap_[0] = heap_.back();
        heap_.pop_back();
        if (!heap_.empty()) {
            heapifyDown(0);
        }
        return true;
    }

    // Remove a specific slot number (used when user manually picks a slot)
    bool remove(int slotNumber) {
        for (size_t i = 0; i < heap_.size(); ++i) {
            if (heap_[i] == slotNumber) {
                heap_[i] = heap_.back();
                heap_.pop_back();
                if (i < heap_.size()) {
                    heapifyUp(i);
                    heapifyDown(i);
                }
                return true;
            }
        }
        return false;
    }

    void rebuild(const std::vector<int>& availableSlots) {
        heap_.clear();
        for (int s : availableSlots) {
            insert(s);
        }
    }

    const std::vector<int>& rawHeap() const { return heap_; }
};

// ============================================================================
// 3. HASH MAP (Separate Chaining) - Fast vehicle lookup by registration number
//    Polynomial rolling hash function mod TABLE_SIZE (prime = 101):
//    Average insert() -> O(1), find() -> O(1), remove() -> O(1)
// ============================================================================
struct VehicleRecord {
    int vehicleId = 0;
    int userId = 0;
    std::string ownerName;
    std::string ownerEmail;
    std::string regNumber;
    std::string vehicleType;
    int activeSlotNumber = 0; // 0 if not currently parked
};

class VehicleHashMap {
private:
    static constexpr size_t TABLE_SIZE = 101;

    struct Entry {
        std::string key; // normalized regNumber
        VehicleRecord value;
    };

    std::vector<std::vector<Entry>> buckets_;
    size_t count_ = 0;

    static size_t hashFunction(const std::string& key) {
        size_t hash = 0;
        for (unsigned char ch : key) {
            hash = (hash * 31 + ch) % TABLE_SIZE;
        }
        return hash;
    }

public:
    VehicleHashMap() : buckets_(TABLE_SIZE) {}

    size_t size() const { return count_; }

    void clear() {
        for (auto& bucket : buckets_) {
            bucket.clear();
        }
        count_ = 0;
    }

    void insert(const VehicleRecord& record) {
        std::string key = normalizeReg(record.regNumber);
        if (key.empty()) return;
        size_t idx = hashFunction(key);
        for (auto& entry : buckets_[idx]) {
            if (entry.key == key) {
                entry.value = record;
                entry.value.regNumber = key;
                return;
            }
        }
        VehicleRecord copy = record;
        copy.regNumber = key;
        buckets_[idx].push_back({key, copy});
        ++count_;
    }

    bool find(const std::string& regNumber, VehicleRecord& out) const {
        std::string key = normalizeReg(regNumber);
        if (key.empty()) return false;
        size_t idx = hashFunction(key);
        for (const auto& entry : buckets_[idx]) {
            if (entry.key == key) {
                out = entry.value;
                return true;
            }
        }
        return false;
    }

    bool remove(const std::string& regNumber) {
        std::string key = normalizeReg(regNumber);
        if (key.empty()) return false;
        size_t idx = hashFunction(key);
        auto& bucket = buckets_[idx];
        for (size_t i = 0; i < bucket.size(); ++i) {
            if (bucket[i].key == key) {
                bucket.erase(bucket.begin() + static_cast<std::ptrdiff_t>(i));
                --count_;
                return true;
            }
        }
        return false;
    }

    std::vector<VehicleRecord> getAll() const {
        std::vector<VehicleRecord> result;
        result.reserve(count_);
        for (const auto& bucket : buckets_) {
            for (const auto& entry : bucket) {
                result.push_back(entry.value);
            }
        }
        std::sort(result.begin(), result.end(), [](const VehicleRecord& a, const VehicleRecord& b) {
            return a.vehicleId < b.vehicleId;
        });
        return result;
    }
};

// ============================================================================
// 4. STACK (LIFO) - Maintain recent parking ENTRY and EXIT operations
//    push() -> O(1), pop() -> O(1), peek() -> O(1)
// ============================================================================
struct ParkingOperation {
    std::string action; // "ENTRY" or "EXIT"
    int slotNumber = 0;
    std::string slotCode;
    std::string regNumber;
    std::string userName;
    std::string timestamp;
};

class OperationStack {
private:
    std::vector<ParkingOperation> stack_;
    static constexpr size_t MAX_HISTORY = 100;

public:
    bool isEmpty() const { return stack_.empty(); }
    size_t size() const { return stack_.size(); }

    void push(const ParkingOperation& op) {
        if (stack_.size() >= MAX_HISTORY) {
            // Keep the most recent operations bounded
            stack_.erase(stack_.begin());
        }
        stack_.push_back(op);
    }

    bool pop(ParkingOperation& out) {
        if (stack_.empty()) return false;
        out = stack_.back();
        stack_.pop_back();
        return true;
    }

    bool peek(ParkingOperation& out) const {
        if (stack_.empty()) return false;
        out = stack_.back();
        return true;
    }

    // Returns up to `limit` most recent operations in LIFO order (top of stack first)
    std::vector<ParkingOperation> getRecent(size_t limit = 20) const {
        std::vector<ParkingOperation> recent;
        size_t count = std::min(limit, stack_.size());
        recent.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            recent.push_back(stack_[stack_.size() - 1 - i]);
        }
        return recent;
    }

    void clear() { stack_.clear(); }
};

} // namespace dsa

