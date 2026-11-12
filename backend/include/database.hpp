#pragma once

#include "auth.hpp"
#include "config.hpp"
#include "dsa.hpp"

#include <mysql.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

struct UserInfo {
    int userId = 0;
    std::string fullName;
    std::string email;
    std::string phone;
    std::string role;
    std::string createdAt;
};

struct SlotInfo {
    int slotId = 0;
    int slotNumber = 0;
    std::string slotCode;
    std::string slotType;
    bool isOccupied = false;
    int activeBookingId = 0;
    int occupiedByUserId = 0;
    std::string occupiedByName;
    std::string occupiedRegNumber;
    std::string entryTime;
};

struct BookingInfo {
    int bookingId = 0;
    int userId = 0;
    std::string fullName;
    std::string email;
    int vehicleId = 0;
    std::string regNumber;
    std::string vehicleType;
    int slotId = 0;
    int slotNumber = 0;
    std::string slotCode;
    std::string slotType;
    std::string entryTime;
    std::string exitTime;
    int durationMinutes = 0;
    std::string status;
};

struct DsaSnapshot {
    int nextLowestAvailableSlot = 0; // 0 if full
    std::vector<int> minHeapArray;
    std::vector<dsa::WaitlistEntry> waitingQueue;
    std::vector<dsa::ParkingOperation> recentStack;
    size_t hashMapVehicleCount = 0;
};

class DatabaseManager {
private:
    AppConfig cfg_;
    MYSQL* conn_ = nullptr;
    mutable std::mutex mu_;

    // Core DSA structures used directly in backend operations
    dsa::SlotMinHeap minHeap_;
    dsa::WaitingQueue waitQueue_;
    dsa::VehicleHashMap vehicleMap_;
    dsa::OperationStack opStack_;

    struct MysqlResDeleter {
        void operator()(MYSQL_RES* res) const {
            if (res) mysql_free_result(res);
        }
    };
    using ResultPtr = std::unique_ptr<MYSQL_RES, MysqlResDeleter>;

    static std::string colStr(MYSQL_ROW row, int idx) {
        return (row && row[idx]) ? std::string(row[idx]) : std::string();
    }

    static int colInt(MYSQL_ROW row, int idx) {
        return (row && row[idx]) ? std::stoi(row[idx]) : 0;
    }

    std::string escape(const std::string& input) {
        if (!conn_) return input;
        std::string out(input.size() * 2 + 1, '\0');
        unsigned long len = mysql_real_escape_string(
            conn_, out.data(), input.c_str(), static_cast<unsigned long>(input.size()));
        out.resize(len);
        return out;
    }

    bool ensureConnected(std::string& errorOut) {
        if (conn_ && mysql_ping(conn_) == 0) {
            return true;
        }
        return connectUnlocked(errorOut);
    }

    bool connectUnlocked(std::string& errorOut) {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            errorOut = "Failed to initialize MySQL handle.";
            return false;
        }
        if (!mysql_real_connect(
                conn_,
                cfg_.dbHost.c_str(),
                cfg_.dbUser.c_str(),
                cfg_.dbPassword.c_str(),
                cfg_.dbName.c_str(),
                cfg_.dbPort,
                nullptr,
                0)) {
            errorOut = std::string("MySQL connection error: ") + mysql_error(conn_);
            mysql_close(conn_);
            conn_ = nullptr;
            return false;
        }
        mysql_set_character_set(conn_, "utf8mb4");
        return true;
    }

    bool execQuery(const std::string& sql, std::string& errorOut) {
        if (mysql_query(conn_, sql.c_str()) != 0) {
            errorOut = std::string("Database query failed: ") + mysql_error(conn_);
            return false;
        }
        return true;
    }

    ResultPtr storeQuery(const std::string& sql, std::string& errorOut) {
        if (mysql_query(conn_, sql.c_str()) != 0) {
            errorOut = std::string("Database query failed: ") + mysql_error(conn_);
            return nullptr;
        }
        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res && mysql_field_count(conn_) > 0) {
            errorOut = std::string("Failed to store result: ") + mysql_error(conn_);
            return nullptr;
        }
        return ResultPtr(res);
    }

    void syncDsaUnlocked() {
        std::string err;

        // 1. Rebuild Min Heap with all currently available slot numbers
        std::vector<int> availableSlots;
        if (auto res = storeQuery(
                "SELECT slot_number FROM Parking_Slots WHERE is_occupied = 0 ORDER BY slot_number ASC", err)) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                availableSlots.push_back(colInt(row, 0));
            }
        }
        minHeap_.rebuild(availableSlots);

        // 2. Rebuild HashMap with all registered vehicles and their active slot (if any)
        vehicleMap_.clear();
        std::string vehSql =
            "SELECT v.vehicle_id, v.user_id, u.full_name, u.email, v.reg_number, v.vehicle_type, "
            "COALESCE(ps.slot_number, 0) AS active_slot "
            "FROM Vehicles v "
            "JOIN Users u ON v.user_id = u.user_id "
            "LEFT JOIN Bookings b ON v.vehicle_id = b.vehicle_id AND b.status = 'active' "
            "LEFT JOIN Parking_Slots ps ON b.slot_id = ps.slot_id";
        if (auto res = storeQuery(vehSql, err)) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                dsa::VehicleRecord rec;
                rec.vehicleId = colInt(row, 0);
                rec.userId = colInt(row, 1);
                rec.ownerName = colStr(row, 2);
                rec.ownerEmail = colStr(row, 3);
                rec.regNumber = colStr(row, 4);
                rec.vehicleType = colStr(row, 5);
                rec.activeSlotNumber = colInt(row, 6);
                vehicleMap_.insert(rec);
            }
        }

        // 3. Warm Operation Stack with recent Parking_History entries (oldest -> newest pushed)
        opStack_.clear();
        std::string histSql =
            "SELECT h.status, h.slot_number, COALESCE(ps.slot_code, CONCAT('P-', LPAD(h.slot_number, 2, '0'))), "
            "h.reg_number, u.full_name, DATE_FORMAT(h.entry_time, '%Y-%m-%d %H:%i:%s'), "
            "DATE_FORMAT(h.exit_time, '%Y-%m-%d %H:%i:%s') "
            "FROM Parking_History h "
            "JOIN Users u ON h.user_id = u.user_id "
            "LEFT JOIN Parking_Slots ps ON h.slot_id = ps.slot_id "
            "ORDER BY h.history_id DESC LIMIT 20";
        if (auto res = storeQuery(histSql, err)) {
            std::vector<dsa::ParkingOperation> temp;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                std::string st = colStr(row, 0);
                int slotNum = colInt(row, 1);
                std::string slotCode = colStr(row, 2);
                std::string reg = colStr(row, 3);
                std::string uname = colStr(row, 4);
                std::string entryT = colStr(row, 5);
                std::string exitT = colStr(row, 6);
                if (st == "completed" && !exitT.empty()) {
                    temp.push_back({"EXIT", slotNum, slotCode, reg, uname, exitT});
                }
                temp.push_back({"ENTRY", slotNum, slotCode, reg, uname, entryT});
            }
            for (auto it = temp.rbegin(); it != temp.rend(); ++it) {
                opStack_.push(*it);
            }
        }
    }

    // Internal helper: creates a booking inside an ALREADY open transaction & lock
    bool createBookingInTxUnlocked(int userId,
                                   const std::string& fullName,
                                   const std::string& email,
                                   int slotId,
                                   int slotNumber,
                                   const std::string& slotCode,
                                   const std::string& slotType,
                                   const std::string& normReg,
                                   const std::string& vehicleType,
                                   BookingInfo& outBooking,
                                   std::string& errorOut) {
        // Check if vehicle already exists
        int vehicleId = 0;
        {
            std::string q = "SELECT vehicle_id, user_id FROM Vehicles WHERE reg_number = '" + escape(normReg) + "' FOR UPDATE";
            auto res = storeQuery(q, errorOut);
            if (!res) return false;
            if (MYSQL_ROW row = mysql_fetch_row(res.get())) {
                vehicleId = colInt(row, 0);
                int ownerId = colInt(row, 1);
                if (ownerId != userId) {
                    errorOut = "Vehicle " + normReg + " is already registered to another user.";
                    return false;
                }
                std::string updV = "UPDATE Vehicles SET vehicle_type = '" + escape(vehicleType) +
                                   "' WHERE vehicle_id = " + std::to_string(vehicleId);
                if (!execQuery(updV, errorOut)) return false;
            } else {
                std::string insV = "INSERT INTO Vehicles (user_id, reg_number, vehicle_type) VALUES (" +
                                   std::to_string(userId) + ", '" + escape(normReg) + "', '" +
                                   escape(vehicleType) + "')";
                if (!execQuery(insV, errorOut)) return false;
                vehicleId = static_cast<int>(mysql_insert_id(conn_));
            }
        }

        // Atomically mark slot as occupied (extra guard against concurrent booking)
        {
            std::string updSlot = "UPDATE Parking_Slots SET is_occupied = 1 WHERE slot_id = " +
                                  std::to_string(slotId) + " AND is_occupied = 0";
            if (!execQuery(updSlot, errorOut)) return false;
            if (mysql_affected_rows(conn_) != 1) {
                errorOut = "Slot " + slotCode + " was just booked by another user.";
                return false;
            }
        }

        // Create active booking
        std::string insB = "INSERT INTO Bookings (user_id, vehicle_id, slot_id, status) VALUES (" +
                           std::to_string(userId) + ", " + std::to_string(vehicleId) + ", " +
                           std::to_string(slotId) + ", 'active')";
        if (!execQuery(insB, errorOut)) return false;
        int bookingId = static_cast<int>(mysql_insert_id(conn_));

        // Create Parking_History entry
        std::string insH =
            "INSERT INTO Parking_History (booking_id, user_id, vehicle_id, slot_id, reg_number, slot_number, entry_time, status) "
            "VALUES (" + std::to_string(bookingId) + ", " + std::to_string(userId) + ", " +
            std::to_string(vehicleId) + ", " + std::to_string(slotId) + ", '" + escape(normReg) + "', " +
            std::to_string(slotNumber) + ", NOW(), 'active')";
        if (!execQuery(insH, errorOut)) return false;

        std::string nowStr = dsa::currentTimestamp();
        outBooking.bookingId = bookingId;
        outBooking.userId = userId;
        outBooking.fullName = fullName;
        outBooking.email = email;
        outBooking.vehicleId = vehicleId;
        outBooking.regNumber = normReg;
        outBooking.vehicleType = vehicleType;
        outBooking.slotId = slotId;
        outBooking.slotNumber = slotNumber;
        outBooking.slotCode = slotCode;
        outBooking.slotType = slotType;
        outBooking.entryTime = nowStr;
        outBooking.status = "active";

        return true;
    }

public:
    explicit DatabaseManager(AppConfig cfg) : cfg_(std::move(cfg)) {}

    ~DatabaseManager() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }

    bool initialize(std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!connectUnlocked(errorOut)) {
            return false;
        }
        syncDsaUnlocked();
        return true;
    }

    bool isConnected() const {
        std::lock_guard<std::mutex> lock(mu_);
        return conn_ != nullptr && mysql_ping(conn_) == 0;
    }

    bool adminExists(bool& existsOut, std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) return false;
        auto res = storeQuery("SELECT COUNT(*) FROM Users WHERE role = 'admin'", errorOut);
        if (!res) return false;
        MYSQL_ROW row = mysql_fetch_row(res.get());
        existsOut = (row && colInt(row, 0) > 0);
        return true;
    }

    bool registerUser(const std::string& fullName,
                      const std::string& email,
                      const std::string& phone,
                      const std::string& password,
                      const std::string& role,
                      UserInfo& userOut,
                      int& httpStatus,
                      std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) {
            httpStatus = 500;
            return false;
        }

        if (role == "admin") {
            auto cntRes = storeQuery("SELECT COUNT(*) FROM Users WHERE role = 'admin'", errorOut);
            if (!cntRes) {
                httpStatus = 500;
                return false;
            }
            MYSQL_ROW r = mysql_fetch_row(cntRes.get());
            if (r && colInt(r, 0) > 0) {
                httpStatus = 403;
                errorOut = "An admin account already exists. Please log in with admin credentials.";
                return false;
            }
        }

        // Check duplicate email
        {
            std::string checkSql = "SELECT user_id FROM Users WHERE email = '" + escape(email) + "'";
            auto res = storeQuery(checkSql, errorOut);
            if (!res) {
                httpStatus = 500;
                return false;
            }
            if (mysql_fetch_row(res.get()) != nullptr) {
                httpStatus = 409;
                errorOut = "An account with this email already exists.";
                return false;
            }
        }

        std::string salt = auth::randomHex(16);
        std::string pwHash = auth::hashPassword(password, salt);

        std::string insSql =
            "INSERT INTO Users (full_name, email, phone, password_hash, salt, role) VALUES ('" +
            escape(fullName) + "', '" + escape(email) + "', '" + escape(phone) + "', '" +
            escape(pwHash) + "', '" + escape(salt) + "', '" + escape(role) + "')";

        if (!execQuery(insSql, errorOut)) {
            httpStatus = 500;
            return false;
        }

        userOut.userId = static_cast<int>(mysql_insert_id(conn_));
        userOut.fullName = fullName;
        userOut.email = email;
        userOut.phone = phone;
        userOut.role = role;
        httpStatus = 201;
        return true;
    }

    bool loginUser(const std::string& email,
                   const std::string& password,
                   const std::string& expectedRole,
                   UserInfo& userOut,
                   int& httpStatus,
                   std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) {
            httpStatus = 500;
            return false;
        }

        std::string sql =
            "SELECT user_id, full_name, email, COALESCE(phone, ''), password_hash, salt, role "
            "FROM Users WHERE email = '" + escape(email) + "' LIMIT 1";
        auto res = storeQuery(sql, errorOut);
        if (!res) {
            httpStatus = 500;
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res.get());
        if (!row) {
            httpStatus = 401;
            errorOut = "Invalid email or password.";
            return false;
        }

        std::string storedHash = colStr(row, 4);
        std::string salt = colStr(row, 5);
        std::string actualRole = colStr(row, 6);

        if (auth::hashPassword(password, salt) != storedHash) {
            httpStatus = 401;
            errorOut = "Invalid email or password.";
            return false;
        }

        if (!expectedRole.empty() && actualRole != expectedRole) {
            httpStatus = 403;
            errorOut = expectedRole == "admin"
                ? "This account does not have administrator privileges."
                : "Please use the Admin Login tab for administrator accounts.";
            return false;
        }

        userOut.userId = colInt(row, 0);
        userOut.fullName = colStr(row, 1);
        userOut.email = colStr(row, 2);
        userOut.phone = colStr(row, 3);
        userOut.role = actualRole;
        httpStatus = 200;
        return true;
    }

    bool getAllSlots(std::vector<SlotInfo>& slotsOut, std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) return false;

        std::string sql =
            "SELECT ps.slot_id, ps.slot_number, ps.slot_code, ps.slot_type, ps.is_occupied, "
            "COALESCE(b.booking_id, 0), COALESCE(b.user_id, 0), COALESCE(u.full_name, ''), "
            "COALESCE(v.reg_number, ''), COALESCE(DATE_FORMAT(b.entry_time, '%Y-%m-%d %H:%i:%s'), '') "
            "FROM Parking_Slots ps "
            "LEFT JOIN Bookings b ON ps.slot_id = b.slot_id AND b.status = 'active' "
            "LEFT JOIN Users u ON b.user_id = u.user_id "
            "LEFT JOIN Vehicles v ON b.vehicle_id = v.vehicle_id "
            "ORDER BY ps.slot_number ASC";

        auto res = storeQuery(sql, errorOut);
        if (!res) return false;

        slotsOut.clear();
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res.get())) != nullptr) {
            SlotInfo s;
            s.slotId = colInt(row, 0);
            s.slotNumber = colInt(row, 1);
            s.slotCode = colStr(row, 2);
            s.slotType = colStr(row, 3);
            s.isOccupied = (colInt(row, 4) != 0);
            s.activeBookingId = colInt(row, 5);
            s.occupiedByUserId = colInt(row, 6);
            s.occupiedByName = colStr(row, 7);
            s.occupiedRegNumber = colStr(row, 8);
            s.entryTime = colStr(row, 9);
            slotsOut.push_back(s);
        }
        return true;
    }

    // Book either a specific slotId (>0) OR auto-allocate lowest slot via MinHeap (slotId == 0)
    bool bookSlot(int userId,
                  const std::string& fullName,
                  const std::string& email,
                  int requestedSlotId,
                  const std::string& rawRegNumber,
                  const std::string& vehicleType,
                  BookingInfo& bookingOut,
                  int& httpStatus,
                  std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) {
            httpStatus = 500;
            return false;
        }

        std::string normReg = dsa::normalizeReg(rawRegNumber);
        if (normReg.size() < 4 || normReg.size() > 20) {
            httpStatus = 400;
            errorOut = "Please enter a valid vehicle registration number (4-20 alphanumeric characters).";
            return false;
        }

        // Prevent same user from holding multiple active bookings simultaneously
        {
            std::string chk = "SELECT booking_id FROM Bookings WHERE user_id = " + std::to_string(userId) +
                              " AND status = 'active' LIMIT 1";
            auto res = storeQuery(chk, errorOut);
            if (!res) { httpStatus = 500; return false; }
            if (mysql_fetch_row(res.get()) != nullptr) {
                httpStatus = 409;
                errorOut = "You already have an active parking booking. Please release your current slot first.";
                return false;
            }
        }

        // Prevent same vehicle from having two active bookings
        {
            std::string chkV =
                "SELECT b.booking_id FROM Bookings b "
                "JOIN Vehicles v ON b.vehicle_id = v.vehicle_id "
                "WHERE v.reg_number = '" + escape(normReg) + "' AND b.status = 'active' LIMIT 1";
            auto res = storeQuery(chkV, errorOut);
            if (!res) { httpStatus = 500; return false; }
            if (mysql_fetch_row(res.get()) != nullptr) {
                httpStatus = 409;
                errorOut = "Vehicle " + normReg + " is already parked in another slot.";
                return false;
            }
        }

        if (!execQuery("START TRANSACTION", errorOut)) {
            httpStatus = 500;
            return false;
        }

        int slotId = 0;
        int slotNumber = 0;
        std::string slotCode;
        std::string slotType;
        bool autoAllocated = (requestedSlotId <= 0);

        if (autoAllocated) {
            int lowestSlotNum = 0;
            if (!minHeap_.peekMin(lowestSlotNum)) {
                execQuery("ROLLBACK", errorOut);
                httpStatus = 409;
                errorOut = "All parking slots are currently occupied. You can join the Waiting List!";
                return false;
            }
            std::string q = "SELECT slot_id, slot_number, slot_code, slot_type, is_occupied "
                            "FROM Parking_Slots WHERE slot_number = " + std::to_string(lowestSlotNum) + " FOR UPDATE";
            auto res = storeQuery(q, errorOut);
            if (!res) { execQuery("ROLLBACK", errorOut); httpStatus = 500; return false; }
            MYSQL_ROW row = mysql_fetch_row(res.get());
            if (!row || colInt(row, 4) != 0) {
                execQuery("ROLLBACK", errorOut);
                syncDsaUnlocked();
                httpStatus = 409;
                errorOut = "Slot state refreshed; please try again.";
                return false;
            }
            slotId = colInt(row, 0);
            slotNumber = colInt(row, 1);
            slotCode = colStr(row, 2);
            slotType = colStr(row, 3);
        } else {
            std::string q = "SELECT slot_id, slot_number, slot_code, slot_type, is_occupied "
                            "FROM Parking_Slots WHERE slot_id = " + std::to_string(requestedSlotId) + " FOR UPDATE";
            auto res = storeQuery(q, errorOut);
            if (!res) { execQuery("ROLLBACK", errorOut); httpStatus = 500; return false; }
            MYSQL_ROW row = mysql_fetch_row(res.get());
            if (!row) {
                execQuery("ROLLBACK", errorOut);
                httpStatus = 404;
                errorOut = "Selected parking slot does not exist.";
                return false;
            }
            if (colInt(row, 4) != 0) {
                execQuery("ROLLBACK", errorOut);
                httpStatus = 409;
                errorOut = "Slot " + colStr(row, 2) + " is already occupied. Please select an available green slot.";
                return false;
            }
            slotId = colInt(row, 0);
            slotNumber = colInt(row, 1);
            slotCode = colStr(row, 2);
            slotType = colStr(row, 3);
        }

        if (!createBookingInTxUnlocked(userId, fullName, email, slotId, slotNumber, slotCode, slotType,
                                       normReg, vehicleType, bookingOut, errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = (errorOut.find("already") != std::string::npos) ? 409 : 500;
            return false;
        }

        if (!execQuery("COMMIT", errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        // Update DSA structures after successful commit
        if (autoAllocated) {
            int popped = 0;
            minHeap_.extractMin(popped);
        } else {
            minHeap_.remove(slotNumber);
        }

        dsa::VehicleRecord vrec;
        vrec.vehicleId = bookingOut.vehicleId;
        vrec.userId = userId;
        vrec.ownerName = fullName;
        vrec.ownerEmail = email;
        vrec.regNumber = normReg;
        vrec.vehicleType = vehicleType;
        vrec.activeSlotNumber = slotNumber;
        vehicleMap_.insert(vrec);

        opStack_.push({"ENTRY", slotNumber, slotCode, normReg, fullName, bookingOut.entryTime});
        waitQueue_.removeByUser(userId);

        httpStatus = 201;
        return true;
    }

    bool releaseSlot(int userId,
                     int bookingId,
                     bool isAdmin,
                     BookingInfo& releasedOut,
                     bool& autoAssignedNext,
                     BookingInfo& nextBookingOut,
                     int& httpStatus,
                     std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        autoAssignedNext = false;
        if (!ensureConnected(errorOut)) {
            httpStatus = 500;
            return false;
        }

        if (!execQuery("START TRANSACTION", errorOut)) {
            httpStatus = 500;
            return false;
        }

        std::string findSql =
            "SELECT b.booking_id, b.user_id, u.full_name, u.email, b.vehicle_id, v.reg_number, v.vehicle_type, "
            "b.slot_id, ps.slot_number, ps.slot_code, ps.slot_type, DATE_FORMAT(b.entry_time, '%Y-%m-%d %H:%i:%s') "
            "FROM Bookings b "
            "JOIN Users u ON b.user_id = u.user_id "
            "JOIN Vehicles v ON b.vehicle_id = v.vehicle_id "
            "JOIN Parking_Slots ps ON b.slot_id = ps.slot_id "
            "WHERE b.booking_id = " + std::to_string(bookingId) + " AND b.status = 'active' FOR UPDATE";

        auto res = storeQuery(findSql, errorOut);
        if (!res) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res.get());
        if (!row) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 404;
            errorOut = "Active booking not found (it may have already been released).";
            return false;
        }

        int ownerId = colInt(row, 1);
        if (!isAdmin && ownerId != userId) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 403;
            errorOut = "You are not authorized to release this booking.";
            return false;
        }

        releasedOut.bookingId = colInt(row, 0);
        releasedOut.userId = ownerId;
        releasedOut.fullName = colStr(row, 2);
        releasedOut.email = colStr(row, 3);
        releasedOut.vehicleId = colInt(row, 4);
        releasedOut.regNumber = colStr(row, 5);
        releasedOut.vehicleType = colStr(row, 6);
        releasedOut.slotId = colInt(row, 7);
        releasedOut.slotNumber = colInt(row, 8);
        releasedOut.slotCode = colStr(row, 9);
        releasedOut.slotType = colStr(row, 10);
        releasedOut.entryTime = colStr(row, 11);
        releasedOut.exitTime = dsa::currentTimestamp();
        releasedOut.status = "completed";

        // Mark booking completed
        if (!execQuery("UPDATE Bookings SET status = 'completed' WHERE booking_id = " + std::to_string(bookingId),
                       errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        // Update Parking_History
        std::string updH =
            "UPDATE Parking_History SET exit_time = NOW(), "
            "duration_minutes = GREATEST(1, TIMESTAMPDIFF(MINUTE, entry_time, NOW())), "
            "status = 'completed' WHERE booking_id = " + std::to_string(bookingId);
        if (!execQuery(updH, errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        // Free the slot
        if (!execQuery("UPDATE Parking_Slots SET is_occupied = 0 WHERE slot_id = " +
                           std::to_string(releasedOut.slotId),
                       errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        // Check FIFO WaitingQueue: if someone is waiting, assign this freed slot automatically!
        dsa::WaitlistEntry waitingUser;
        bool poppedFromQueue = false;
        if (!waitQueue_.isEmpty()) {
            // Peek first so we only dequeue after DB commit succeeds
            if (waitQueue_.peek(waitingUser)) {
                // Fetch waiting user's email
                std::string uEmail;
                if (auto uRes = storeQuery("SELECT email FROM Users WHERE user_id = " +
                                               std::to_string(waitingUser.userId) + " LIMIT 1",
                                           errorOut)) {
                    if (MYSQL_ROW ur = mysql_fetch_row(uRes.get())) {
                        uEmail = colStr(ur, 0);
                    }
                }
                std::string assignErr;
                if (createBookingInTxUnlocked(waitingUser.userId, waitingUser.fullName, uEmail,
                                              releasedOut.slotId, releasedOut.slotNumber,
                                              releasedOut.slotCode, releasedOut.slotType,
                                              waitingUser.regNumber, waitingUser.vehicleType,
                                              nextBookingOut, assignErr)) {
                    autoAssignedNext = true;
                    poppedFromQueue = true;
                }
            }
        }

        if (!execQuery("COMMIT", errorOut)) {
            std::string dummy;
            execQuery("ROLLBACK", dummy);
            httpStatus = 500;
            return false;
        }

        // Update DSA structures post-commit
        dsa::VehicleRecord releasedVeh;
        if (vehicleMap_.find(releasedOut.regNumber, releasedVeh)) {
            releasedVeh.activeSlotNumber = 0;
            vehicleMap_.insert(releasedVeh);
        }
        opStack_.push({"EXIT", releasedOut.slotNumber, releasedOut.slotCode,
                       releasedOut.regNumber, releasedOut.fullName, releasedOut.exitTime});

        if (autoAssignedNext && poppedFromQueue) {
            dsa::WaitlistEntry dequeued;
            waitQueue_.dequeue(dequeued);

            dsa::VehicleRecord nextVeh;
            nextVeh.vehicleId = nextBookingOut.vehicleId;
            nextVeh.userId = nextBookingOut.userId;
            nextVeh.ownerName = nextBookingOut.fullName;
            nextVeh.ownerEmail = nextBookingOut.email;
            nextVeh.regNumber = nextBookingOut.regNumber;
            nextVeh.vehicleType = nextBookingOut.vehicleType;
            nextVeh.activeSlotNumber = nextBookingOut.slotNumber;
            vehicleMap_.insert(nextVeh);

            opStack_.push({"ENTRY", nextBookingOut.slotNumber, nextBookingOut.slotCode,
                           nextBookingOut.regNumber, nextBookingOut.fullName, nextBookingOut.entryTime});
        } else {
            minHeap_.insert(releasedOut.slotNumber);
        }

        httpStatus = 200;
        return true;
    }

    bool joinWaitlist(int userId,
                      const std::string& fullName,
                      const std::string& rawRegNumber,
                      const std::string& vehicleType,
                      int& positionOut,
                      int& httpStatus,
                      std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) {
            httpStatus = 500;
            return false;
        }

        if (!minHeap_.isEmpty()) {
            httpStatus = 409;
            errorOut = "Parking slots are currently available! Please book an available slot directly.";
            return false;
        }

        std::string normReg = dsa::normalizeReg(rawRegNumber);
        if (normReg.size() < 4 || normReg.size() > 20) {
            httpStatus = 400;
            errorOut = "Please enter a valid vehicle registration number (4-20 alphanumeric characters).";
            return false;
        }

        if (waitQueue_.containsUser(userId)) {
            httpStatus = 409;
            errorOut = "You are already on the waiting list at position #" +
                       std::to_string(waitQueue_.positionOfUser(userId)) + ".";
            return false;
        }

        // Check active booking
        auto res = storeQuery("SELECT booking_id FROM Bookings WHERE user_id = " + std::to_string(userId) +
                                  " AND status = 'active' LIMIT 1",
                              errorOut);
        if (!res) { httpStatus = 500; return false; }
        if (mysql_fetch_row(res.get()) != nullptr) {
            httpStatus = 409;
            errorOut = "You already have an active parking booking.";
            return false;
        }

        waitQueue_.enqueue({userId, fullName, normReg, vehicleType, dsa::currentTimestamp()});
        positionOut = waitQueue_.positionOfUser(userId);
        httpStatus = 201;
        return true;
    }

    bool leaveWaitlist(int userId) {
        std::lock_guard<std::mutex> lock(mu_);
        return waitQueue_.removeByUser(userId);
    }

    int getUserWaitlistPosition(int userId) const {
        std::lock_guard<std::mutex> lock(mu_);
        return waitQueue_.positionOfUser(userId);
    }

    bool getUserBookings(int userId,
                         std::vector<BookingInfo>& activeOut,
                         std::vector<BookingInfo>& historyOut,
                         std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) return false;

        activeOut.clear();
        historyOut.clear();

        std::string activeSql =
            "SELECT b.booking_id, b.user_id, u.full_name, u.email, v.vehicle_id, v.reg_number, v.vehicle_type, "
            "ps.slot_id, ps.slot_number, ps.slot_code, ps.slot_type, DATE_FORMAT(b.entry_time, '%Y-%m-%d %H:%i:%s'), "
            "b.status "
            "FROM Bookings b "
            "JOIN Users u ON b.user_id = u.user_id "
            "JOIN Vehicles v ON b.vehicle_id = v.vehicle_id "
            "JOIN Parking_Slots ps ON b.slot_id = ps.slot_id "
            "WHERE b.user_id = " + std::to_string(userId) + " AND b.status = 'active' "
            "ORDER BY b.booking_id DESC";

        if (auto res = storeQuery(activeSql, errorOut)) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                BookingInfo b;
                b.bookingId = colInt(row, 0);
                b.userId = colInt(row, 1);
                b.fullName = colStr(row, 2);
                b.email = colStr(row, 3);
                b.vehicleId = colInt(row, 4);
                b.regNumber = colStr(row, 5);
                b.vehicleType = colStr(row, 6);
                b.slotId = colInt(row, 7);
                b.slotNumber = colInt(row, 8);
                b.slotCode = colStr(row, 9);
                b.slotType = colStr(row, 10);
                b.entryTime = colStr(row, 11);
                b.status = colStr(row, 12);
                activeOut.push_back(b);
            }
        } else {
            return false;
        }

        std::string histSql =
            "SELECT h.booking_id, h.user_id, u.full_name, u.email, h.vehicle_id, h.reg_number, v.vehicle_type, "
            "h.slot_id, h.slot_number, COALESCE(ps.slot_code, CONCAT('P-', LPAD(h.slot_number, 2, '0'))), "
            "COALESCE(ps.slot_type, 'Car'), DATE_FORMAT(h.entry_time, '%Y-%m-%d %H:%i:%s'), "
            "COALESCE(DATE_FORMAT(h.exit_time, '%Y-%m-%d %H:%i:%s'), ''), COALESCE(h.duration_minutes, 0), h.status "
            "FROM Parking_History h "
            "JOIN Users u ON h.user_id = u.user_id "
            "JOIN Vehicles v ON h.vehicle_id = v.vehicle_id "
            "LEFT JOIN Parking_Slots ps ON h.slot_id = ps.slot_id "
            "WHERE h.user_id = " + std::to_string(userId) + " "
            "ORDER BY h.history_id DESC LIMIT 50";

        if (auto res = storeQuery(histSql, errorOut)) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                BookingInfo b;
                b.bookingId = colInt(row, 0);
                b.userId = colInt(row, 1);
                b.fullName = colStr(row, 2);
                b.email = colStr(row, 3);
                b.vehicleId = colInt(row, 4);
                b.regNumber = colStr(row, 5);
                b.vehicleType = colStr(row, 6);
                b.slotId = colInt(row, 7);
                b.slotNumber = colInt(row, 8);
                b.slotCode = colStr(row, 9);
                b.slotType = colStr(row, 10);
                b.entryTime = colStr(row, 11);
                b.exitTime = colStr(row, 12);
                b.durationMinutes = colInt(row, 13);
                b.status = colStr(row, 14);
                historyOut.push_back(b);
            }
        } else {
            return false;
        }

        return true;
    }

    // ==================== ADMIN OPERATIONS ====================

    bool addSlot(int slotNumber,
                 const std::string& slotCode,
                 const std::string& slotType,
                 SlotInfo& createdOut,
                 int& httpStatus,
                 std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) { httpStatus = 500; return false; }

        if (slotNumber <= 0 || slotNumber > 9999) {
            httpStatus = 400;
            errorOut = "Slot number must be between 1 and 9999.";
            return false;
        }

        std::string chk = "SELECT slot_id FROM Parking_Slots WHERE slot_number = " +
                          std::to_string(slotNumber) + " OR slot_code = '" + escape(slotCode) + "'";
        auto res = storeQuery(chk, errorOut);
        if (!res) { httpStatus = 500; return false; }
        if (mysql_fetch_row(res.get()) != nullptr) {
            httpStatus = 409;
            errorOut = "A parking slot with this number or code already exists.";
            return false;
        }

        std::string ins = "INSERT INTO Parking_Slots (slot_number, slot_code, slot_type, is_occupied) VALUES (" +
                          std::to_string(slotNumber) + ", '" + escape(slotCode) + "', '" +
                          escape(slotType) + "', 0)";
        if (!execQuery(ins, errorOut)) {
            httpStatus = 500;
            return false;
        }

        createdOut.slotId = static_cast<int>(mysql_insert_id(conn_));
        createdOut.slotNumber = slotNumber;
        createdOut.slotCode = slotCode;
        createdOut.slotType = slotType;
        createdOut.isOccupied = false;

        minHeap_.insert(slotNumber);

        // If someone is waiting in the FIFO queue, notify? Wait: we keep it clean or auto-assign!
        // Keeping addSlot straightforward inserts it into MinHeap.
        httpStatus = 201;
        return true;
    }

    bool removeSlot(int slotId, int& httpStatus, std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) { httpStatus = 500; return false; }

        std::string chk = "SELECT slot_number, slot_code, is_occupied FROM Parking_Slots WHERE slot_id = " +
                          std::to_string(slotId);
        auto res = storeQuery(chk, errorOut);
        if (!res) { httpStatus = 500; return false; }
        MYSQL_ROW row = mysql_fetch_row(res.get());
        if (!row) {
            httpStatus = 404;
            errorOut = "Parking slot not found.";
            return false;
        }
        int slotNum = colInt(row, 0);
        std::string slotCode = colStr(row, 1);
        bool occupied = (colInt(row, 2) != 0);
        if (occupied) {
            httpStatus = 409;
            errorOut = "Cannot remove slot " + slotCode + " while it is currently occupied.";
            return false;
        }

        if (!execQuery("DELETE FROM Parking_Slots WHERE slot_id = " + std::to_string(slotId), errorOut)) {
            httpStatus = 500;
            return false;
        }

        minHeap_.remove(slotNum);
        httpStatus = 200;
        return true;
    }

    bool adminGetUsers(std::vector<UserInfo>& usersOut, std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) return false;

        std::string sql =
            "SELECT user_id, full_name, email, COALESCE(phone, ''), role, "
            "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') FROM Users ORDER BY user_id ASC";
        auto res = storeQuery(sql, errorOut);
        if (!res) return false;

        usersOut.clear();
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res.get())) != nullptr) {
            usersOut.push_back({
                colInt(row, 0), colStr(row, 1), colStr(row, 2),
                colStr(row, 3), colStr(row, 4), colStr(row, 5)
            });
        }
        return true;
    }

    std::vector<dsa::VehicleRecord> adminGetVehicles() const {
        std::lock_guard<std::mutex> lock(mu_);
        return vehicleMap_.getAll();
    }

    // Uses O(1) HashMap lookup when exact registration is provided, with partial match fallback
    std::vector<dsa::VehicleRecord> adminSearchVehicles(const std::string& query, bool& exactHashHit) const {
        std::lock_guard<std::mutex> lock(mu_);
        std::string norm = dsa::normalizeReg(query);
        exactHashHit = false;
        if (norm.empty()) {
            return vehicleMap_.getAll();
        }
        dsa::VehicleRecord exact;
        if (vehicleMap_.find(norm, exact)) {
            exactHashHit = true;
            return {exact};
        }
        std::vector<dsa::VehicleRecord> partial;
        for (const auto& rec : vehicleMap_.getAll()) {
            if (rec.regNumber.find(norm) != std::string::npos) {
                partial.push_back(rec);
            }
        }
        return partial;
    }

    bool adminGetBookings(bool activeOnly, std::vector<BookingInfo>& listOut, std::string& errorOut) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!ensureConnected(errorOut)) return false;

        listOut.clear();
        if (activeOnly) {
            std::string sql =
                "SELECT b.booking_id, b.user_id, u.full_name, u.email, v.vehicle_id, v.reg_number, v.vehicle_type, "
                "ps.slot_id, ps.slot_number, ps.slot_code, ps.slot_type, DATE_FORMAT(b.entry_time, '%Y-%m-%d %H:%i:%s'), "
                "b.status "
                "FROM Bookings b "
                "JOIN Users u ON b.user_id = u.user_id "
                "JOIN Vehicles v ON b.vehicle_id = v.vehicle_id "
                "JOIN Parking_Slots ps ON b.slot_id = ps.slot_id "
                "WHERE b.status = 'active' ORDER BY b.booking_id DESC";
            auto res = storeQuery(sql, errorOut);
            if (!res) return false;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                BookingInfo b;
                b.bookingId = colInt(row, 0);
                b.userId = colInt(row, 1);
                b.fullName = colStr(row, 2);
                b.email = colStr(row, 3);
                b.vehicleId = colInt(row, 4);
                b.regNumber = colStr(row, 5);
                b.vehicleType = colStr(row, 6);
                b.slotId = colInt(row, 7);
                b.slotNumber = colInt(row, 8);
                b.slotCode = colStr(row, 9);
                b.slotType = colStr(row, 10);
                b.entryTime = colStr(row, 11);
                b.status = colStr(row, 12);
                listOut.push_back(b);
            }
        } else {
            std::string sql =
                "SELECT h.booking_id, h.user_id, u.full_name, u.email, h.vehicle_id, h.reg_number, v.vehicle_type, "
                "h.slot_id, h.slot_number, COALESCE(ps.slot_code, CONCAT('P-', LPAD(h.slot_number, 2, '0'))), "
                "COALESCE(ps.slot_type, 'Car'), DATE_FORMAT(h.entry_time, '%Y-%m-%d %H:%i:%s'), "
                "COALESCE(DATE_FORMAT(h.exit_time, '%Y-%m-%d %H:%i:%s'), ''), COALESCE(h.duration_minutes, 0), h.status "
                "FROM Parking_History h "
                "JOIN Users u ON h.user_id = u.user_id "
                "JOIN Vehicles v ON h.vehicle_id = v.vehicle_id "
                "LEFT JOIN Parking_Slots ps ON h.slot_id = ps.slot_id "
                "ORDER BY h.history_id DESC LIMIT 100";
            auto res = storeQuery(sql, errorOut);
            if (!res) return false;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res.get())) != nullptr) {
                BookingInfo b;
                b.bookingId = colInt(row, 0);
                b.userId = colInt(row, 1);
                b.fullName = colStr(row, 2);
                b.email = colStr(row, 3);
                b.vehicleId = colInt(row, 4);
                b.regNumber = colStr(row, 5);
                b.vehicleType = colStr(row, 6);
                b.slotId = colInt(row, 7);
                b.slotNumber = colInt(row, 8);
                b.slotCode = colStr(row, 9);
                b.slotType = colStr(row, 10);
                b.entryTime = colStr(row, 11);
                b.exitTime = colStr(row, 12);
                b.durationMinutes = colInt(row, 13);
                b.status = colStr(row, 14);
                listOut.push_back(b);
            }
        }
        return true;
    }

    DsaSnapshot getDsaSnapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        DsaSnapshot snap;
        int minSlot = 0;
        if (minHeap_.peekMin(minSlot)) {
            snap.nextLowestAvailableSlot = minSlot;
        }
        snap.minHeapArray = minHeap_.rawHeap();
        snap.waitingQueue = waitQueue_.toList();
        snap.recentStack = opStack_.getRecent(15);
        snap.hashMapVehicleCount = vehicleMap_.size();
        return snap;
    }
};

