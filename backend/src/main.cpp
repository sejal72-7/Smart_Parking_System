#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif

#define ASIO_STANDALONE
#include "crow_all.h"

#include "auth.hpp"
#include "config.hpp"
#include "database.hpp"
#include "dsa.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static crow::response jsonError(int status, const std::string& message) {
    crow::json::wvalue body;
    body["ok"] = false;
    body["error"] = message;
    crow::response res(status, body);
    res.add_header("Content-Type", "application/json");
    return res;
}

static crow::response jsonOk(crow::json::wvalue body, int status = 200) {
    body["ok"] = true;
    crow::response res(status, body);
    res.add_header("Content-Type", "application/json");
    return res;
}

static std::string extractBearerToken(const crow::request& req) {
    std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.rfind("Bearer ", 0) == 0) {
        return authHeader.substr(7);
    }
    return "";
}

static bool isValidEmail(const std::string& email) {
    static const std::regex pattern(R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)");
    return std::regex_match(email, pattern);
}

static std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string toLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string getMimeType(const fs::path& p) {
    std::string ext = toLowerStr(p.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

static crow::response serveStaticFile(const fs::path& baseDir, const std::string& relPath) {
    fs::path cleanBase = fs::weakly_canonical(baseDir);
    fs::path target = fs::weakly_canonical(cleanBase / relPath);

    // Prevent directory traversal outside frontend/
    std::string baseStr = cleanBase.string();
    std::string targetStr = target.string();
    if (targetStr.rfind(baseStr, 0) != 0 || !fs::exists(target) || !fs::is_regular_file(target)) {
        return crow::response(404, "Not Found");
    }

    std::ifstream in(target, std::ios::binary);
    if (!in) {
        return crow::response(500, "Failed to read file");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    crow::response res(200, ss.str());
    res.add_header("Content-Type", getMimeType(target));
    return res;
}

static crow::json::wvalue bookingToJson(const BookingInfo& b) {
    crow::json::wvalue item;
    item["bookingId"] = b.bookingId;
    item["userId"] = b.userId;
    item["fullName"] = b.fullName;
    item["email"] = b.email;
    item["vehicleId"] = b.vehicleId;
    item["regNumber"] = b.regNumber;
    item["vehicleType"] = b.vehicleType;
    item["slotId"] = b.slotId;
    item["slotNumber"] = b.slotNumber;
    item["slotCode"] = b.slotCode;
    item["slotType"] = b.slotType;
    item["entryTime"] = b.entryTime;
    item["exitTime"] = b.exitTime;
    item["durationMinutes"] = b.durationMinutes;
    item["status"] = b.status;
    return item;
}

int main() {
    AppConfig cfg = AppConfig::load("config.env");
    DatabaseManager db(cfg);
    auth::SessionManager sessions;

    std::string initErr;
    bool dbReady = db.initialize(initErr);
    if (!dbReady) {
        std::cerr << "[WARN] Initial MySQL connection failed: " << initErr << "\n"
                  << "       Update backend/config.env with your MySQL credentials and ensure schema.sql is imported.\n";
    } else {
        std::cout << "[OK] Connected to MySQL database '" << cfg.dbName << "' on "
                  << cfg.dbHost << ":" << cfg.dbPort << "\n";
    }

    fs::path frontendRoot = fs::path(cfg.frontendDir);
    if (!fs::exists(frontendRoot)) {
        std::cerr << "[WARN] Frontend directory not found at: " << frontendRoot << "\n";
    } else {
        std::cout << "[OK] Serving frontend from: " << fs::weakly_canonical(frontendRoot) << "\n";
    }

    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global()
        .headers("Content-Type", "Authorization")
        .methods("GET"_method, "POST"_method, "DELETE"_method, "OPTIONS"_method)
        .origin("*");

    // ==================== HEALTH CHECK ====================
    CROW_ROUTE(app, "/api/health").methods("GET"_method)([&db]() {
        crow::json::wvalue res;
        res["dbConnected"] = db.isConnected();
        return jsonOk(std::move(res));
    });

    // ==================== AUTHENTICATION ====================
    CROW_ROUTE(app, "/api/auth/admin-exists").methods("GET"_method)([&db]() {
        bool exists = false;
        std::string err;
        if (!db.adminExists(exists, err)) {
            return jsonError(500, err);
        }
        crow::json::wvalue res;
        res["exists"] = exists;
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        std::string fullName = body.has("fullName") ? trimStr(body["fullName"].s()) : "";
        std::string email = body.has("email") ? toLowerStr(trimStr(body["email"].s())) : "";
        std::string phone = body.has("phone") ? trimStr(body["phone"].s()) : "";
        std::string password = body.has("password") ? std::string(body["password"].s()) : "";

        if (fullName.size() < 2) return jsonError(400, "Full name must be at least 2 characters.");
        if (!isValidEmail(email)) return jsonError(400, "Please provide a valid email address.");
        if (password.size() < 6) return jsonError(400, "Password must be at least 6 characters long.");

        UserInfo user;
        int status = 201;
        std::string err;
        if (!db.registerUser(fullName, email, phone, password, "user", user, status, err)) {
            return jsonError(status, err);
        }

        crow::json::wvalue res;
        res["message"] = "Registration successful! You can now log in.";
        res["user"]["userId"] = user.userId;
        res["user"]["fullName"] = user.fullName;
        res["user"]["email"] = user.email;
        res["user"]["role"] = user.role;
        return jsonOk(std::move(res), 201);
    });

    CROW_ROUTE(app, "/api/auth/bootstrap-admin").methods("POST"_method)([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        std::string fullName = body.has("fullName") ? trimStr(body["fullName"].s()) : "";
        std::string email = body.has("email") ? toLowerStr(trimStr(body["email"].s())) : "";
        std::string phone = body.has("phone") ? trimStr(body["phone"].s()) : "";
        std::string password = body.has("password") ? std::string(body["password"].s()) : "";

        if (fullName.size() < 2) return jsonError(400, "Admin name must be at least 2 characters.");
        if (!isValidEmail(email)) return jsonError(400, "Please provide a valid email address.");
        if (password.size() < 6) return jsonError(400, "Password must be at least 6 characters long.");

        UserInfo user;
        int status = 201;
        std::string err;
        if (!db.registerUser(fullName, email, phone, password, "admin", user, status, err)) {
            return jsonError(status, err);
        }

        crow::json::wvalue res;
        res["message"] = "First-time Admin account created! Please log in.";
        return jsonOk(std::move(res), 201);
    });

    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)([&db, &sessions](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        std::string email = body.has("email") ? toLowerStr(trimStr(body["email"].s())) : "";
        std::string password = body.has("password") ? std::string(body["password"].s()) : "";
        std::string expectedRole = body.has("role") ? toLowerStr(trimStr(body["role"].s())) : "user";

        if (!isValidEmail(email) || password.empty()) {
            return jsonError(400, "Email and password are required.");
        }
        if (expectedRole != "user" && expectedRole != "admin") {
            expectedRole = "user";
        }

        UserInfo user;
        int status = 200;
        std::string err;
        if (!db.loginUser(email, password, expectedRole, user, status, err)) {
            return jsonError(status, err);
        }

        std::string token = sessions.createSession(user.userId, user.fullName, user.email, user.role);
        crow::json::wvalue res;
        res["token"] = token;
        res["user"]["userId"] = user.userId;
        res["user"]["fullName"] = user.fullName;
        res["user"]["email"] = user.email;
        res["user"]["role"] = user.role;
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)([&sessions](const crow::request& req) {
        std::string token = extractBearerToken(req);
        if (!token.empty()) sessions.remove(token);
        crow::json::wvalue res;
        res["message"] = "Logged out successfully.";
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/auth/me").methods("GET"_method)([&sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }
        crow::json::wvalue res;
        res["user"]["userId"] = su.userId;
        res["user"]["fullName"] = su.fullName;
        res["user"]["email"] = su.email;
        res["user"]["role"] = su.role;
        return jsonOk(std::move(res));
    });

    // ==================== SLOTS & USER BOOKINGS ====================
    CROW_ROUTE(app, "/api/slots").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }

        std::vector<SlotInfo> slots;
        std::string err;
        if (!db.getAllSlots(slots, err)) {
            return jsonError(500, err);
        }

        int total = static_cast<int>(slots.size());
        int occupied = 0;
        for (const auto& s : slots) {
            if (s.isOccupied) ++occupied;
        }
        int available = total - occupied;
        DsaSnapshot snap = db.getDsaSnapshot();

        crow::json::wvalue res;
        res["summary"]["total"] = total;
        res["summary"]["available"] = available;
        res["summary"]["occupied"] = occupied;
        res["summary"]["nextLowestSlot"] = snap.nextLowestAvailableSlot;
        res["summary"]["waitlistCount"] = static_cast<int>(snap.waitingQueue.size());

        std::vector<crow::json::wvalue> slotArr;
        slotArr.reserve(slots.size());
        for (const auto& s : slots) {
            crow::json::wvalue item;
            item["slotId"] = s.slotId;
            item["slotNumber"] = s.slotNumber;
            item["slotCode"] = s.slotCode;
            item["slotType"] = s.slotType;
            item["isOccupied"] = s.isOccupied;
            bool isMine = (s.isOccupied && s.occupiedByUserId == su.userId);
            item["isMine"] = isMine;
            if (su.role == "admin" || isMine) {
                item["activeBookingId"] = s.activeBookingId;
                item["occupiedByName"] = s.occupiedByName;
                item["occupiedRegNumber"] = s.occupiedRegNumber;
                item["entryTime"] = s.entryTime;
            }
            slotArr.push_back(std::move(item));
        }
        res["slots"] = std::move(slotArr);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/bookings").methods("POST"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }

        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        int slotId = (body.has("slotId") && body["slotId"].t() == crow::json::type::Number)
            ? static_cast<int>(body["slotId"].i())
            : 0;
        std::string regNumber = body.has("regNumber") ? std::string(body["regNumber"].s()) : "";
        std::string vehicleType = body.has("vehicleType") ? trimStr(body["vehicleType"].s()) : "Car";
        if (vehicleType != "Car" && vehicleType != "Bike" && vehicleType != "SUV" && vehicleType != "EV") {
            vehicleType = "Car";
        }

        BookingInfo created;
        int status = 201;
        std::string err;
        if (!db.bookSlot(su.userId, su.fullName, su.email, slotId, regNumber, vehicleType,
                         created, status, err)) {
            return jsonError(status, err);
        }

        crow::json::wvalue res;
        res["message"] = (slotId == 0)
            ? ("Auto-allocated lowest available slot " + created.slotCode + " using Min Heap!")
            : ("Booked slot " + created.slotCode + " successfully!");
        res["booking"] = bookingToJson(created);
        return jsonOk(std::move(res), 201);
    });

    CROW_ROUTE(app, "/api/bookings/<int>/release").methods("POST"_method)(
        [&db, &sessions](const crow::request& req, int bookingId) {
            auth::SessionUser su;
            if (!sessions.validate(extractBearerToken(req), su)) {
                return jsonError(401, "Unauthorized. Please log in.");
            }

            BookingInfo released;
            BookingInfo nextAssigned;
            bool autoAssignedNext = false;
            int status = 200;
            std::string err;
            if (!db.releaseSlot(su.userId, bookingId, su.role == "admin",
                                released, autoAssignedNext, nextAssigned, status, err)) {
                return jsonError(status, err);
            }

            crow::json::wvalue res;
            res["message"] = "Slot " + released.slotCode + " released successfully.";
            res["released"] = bookingToJson(released);
            res["autoAssignedNext"] = autoAssignedNext;
            if (autoAssignedNext) {
                res["nextBooking"] = bookingToJson(nextAssigned);
            }
            return jsonOk(std::move(res));
        });

    CROW_ROUTE(app, "/api/bookings/my").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }

        std::vector<BookingInfo> active, history;
        std::string err;
        if (!db.getUserBookings(su.userId, active, history, err)) {
            return jsonError(500, err);
        }

        crow::json::wvalue res;
        std::vector<crow::json::wvalue> actArr, histArr;
        for (const auto& b : active) actArr.push_back(bookingToJson(b));
        for (const auto& b : history) histArr.push_back(bookingToJson(b));
        res["active"] = std::move(actArr);
        res["history"] = std::move(histArr);
        res["waitlistPosition"] = db.getUserWaitlistPosition(su.userId);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/waitlist").methods("POST"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }
        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        std::string regNumber = body.has("regNumber") ? std::string(body["regNumber"].s()) : "";
        std::string vehicleType = body.has("vehicleType") ? trimStr(body["vehicleType"].s()) : "Car";
        if (vehicleType != "Car" && vehicleType != "Bike" && vehicleType != "SUV" && vehicleType != "EV") {
            vehicleType = "Car";
        }

        int pos = 0;
        int status = 201;
        std::string err;
        if (!db.joinWaitlist(su.userId, su.fullName, regNumber, vehicleType, pos, status, err)) {
            return jsonError(status, err);
        }

        crow::json::wvalue res;
        res["message"] = "Added to FIFO Waiting List at position #" + std::to_string(pos) + ".";
        res["position"] = pos;
        return jsonOk(std::move(res), 201);
    });

    CROW_ROUTE(app, "/api/waitlist").methods("DELETE"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) {
            return jsonError(401, "Unauthorized. Please log in.");
        }
        bool removed = db.leaveWaitlist(su.userId);
        crow::json::wvalue res;
        res["removed"] = removed;
        res["message"] = removed ? "Removed from waiting list." : "You were not in the waiting list.";
        return jsonOk(std::move(res));
    });

    // ==================== ADMIN ENDPOINTS ====================
    CROW_ROUTE(app, "/api/admin/slots").methods("POST"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        auto body = crow::json::load(req.body);
        if (!body) return jsonError(400, "Invalid JSON payload.");

        int slotNumber = body.has("slotNumber") ? static_cast<int>(body["slotNumber"].i()) : 0;
        std::string slotCode = body.has("slotCode") ? trimStr(body["slotCode"].s()) : "";
        std::string slotType = body.has("slotType") ? trimStr(body["slotType"].s()) : "Car";
        if (slotCode.empty() && slotNumber > 0) {
            std::ostringstream oss;
            oss << "P-" << std::setw(2) << std::setfill('0') << slotNumber;
            slotCode = oss.str();
        }
        if (slotType != "Car" && slotType != "Bike" && slotType != "EV") {
            slotType = "Car";
        }

        SlotInfo created;
        int status = 201;
        std::string err;
        if (!db.addSlot(slotNumber, slotCode, slotType, created, status, err)) {
            return jsonError(status, err);
        }

        crow::json::wvalue res;
        res["message"] = "Parking slot " + created.slotCode + " added.";
        res["slot"]["slotId"] = created.slotId;
        res["slot"]["slotNumber"] = created.slotNumber;
        res["slot"]["slotCode"] = created.slotCode;
        res["slot"]["slotType"] = created.slotType;
        res["slot"]["isOccupied"] = created.isOccupied;
        return jsonOk(std::move(res), 201);
    });

    CROW_ROUTE(app, "/api/admin/slots/<int>").methods("DELETE"_method)(
        [&db, &sessions](const crow::request& req, int slotId) {
            auth::SessionUser su;
            if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
            if (su.role != "admin") return jsonError(403, "Admin access required.");

            int status = 200;
            std::string err;
            if (!db.removeSlot(slotId, status, err)) {
                return jsonError(status, err);
            }
            crow::json::wvalue res;
            res["message"] = "Parking slot removed.";
            return jsonOk(std::move(res));
        });

    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        std::vector<UserInfo> users;
        std::string err;
        if (!db.adminGetUsers(users, err)) return jsonError(500, err);

        std::vector<crow::json::wvalue> arr;
        for (const auto& u : users) {
            crow::json::wvalue item;
            item["userId"] = u.userId;
            item["fullName"] = u.fullName;
            item["email"] = u.email;
            item["phone"] = u.phone;
            item["role"] = u.role;
            item["createdAt"] = u.createdAt;
            arr.push_back(std::move(item));
        }
        crow::json::wvalue res;
        res["users"] = std::move(arr);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/admin/vehicles").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        const char* qParam = req.url_params.get("q");
        std::string q = qParam ? std::string(qParam) : "";
        bool exactHashHit = false;
        auto vehicles = q.empty() ? db.adminGetVehicles() : db.adminSearchVehicles(q, exactHashHit);

        std::vector<crow::json::wvalue> arr;
        for (const auto& v : vehicles) {
            crow::json::wvalue item;
            item["vehicleId"] = v.vehicleId;
            item["userId"] = v.userId;
            item["ownerName"] = v.ownerName;
            item["ownerEmail"] = v.ownerEmail;
            item["regNumber"] = v.regNumber;
            item["vehicleType"] = v.vehicleType;
            item["activeSlotNumber"] = v.activeSlotNumber;
            arr.push_back(std::move(item));
        }
        crow::json::wvalue res;
        res["exactHashHit"] = exactHashHit;
        res["vehicles"] = std::move(arr);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/admin/bookings/active").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        std::vector<BookingInfo> list;
        std::string err;
        if (!db.adminGetBookings(true, list, err)) return jsonError(500, err);

        std::vector<crow::json::wvalue> arr;
        for (const auto& b : list) arr.push_back(bookingToJson(b));
        crow::json::wvalue res;
        res["bookings"] = std::move(arr);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/admin/history").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        std::vector<BookingInfo> list;
        std::string err;
        if (!db.adminGetBookings(false, list, err)) return jsonError(500, err);

        std::vector<crow::json::wvalue> arr;
        for (const auto& b : list) arr.push_back(bookingToJson(b));
        crow::json::wvalue res;
        res["history"] = std::move(arr);
        return jsonOk(std::move(res));
    });

    CROW_ROUTE(app, "/api/admin/dsa").methods("GET"_method)([&db, &sessions](const crow::request& req) {
        auth::SessionUser su;
        if (!sessions.validate(extractBearerToken(req), su)) return jsonError(401, "Unauthorized.");
        if (su.role != "admin") return jsonError(403, "Admin access required.");

        DsaSnapshot snap = db.getDsaSnapshot();
        crow::json::wvalue res;
        res["minHeap"]["nextLowestSlot"] = snap.nextLowestAvailableSlot;
        std::vector<int> heapVals = snap.minHeapArray;
        res["minHeap"]["heapArray"] = std::move(heapVals);

        std::vector<crow::json::wvalue> qArr;
        for (size_t i = 0; i < snap.waitingQueue.size(); ++i) {
            const auto& w = snap.waitingQueue[i];
            crow::json::wvalue item;
            item["position"] = static_cast<int>(i + 1);
            item["userId"] = w.userId;
            item["fullName"] = w.fullName;
            item["regNumber"] = w.regNumber;
            item["vehicleType"] = w.vehicleType;
            item["requestedAt"] = w.requestedAt;
            qArr.push_back(std::move(item));
        }
        res["waitingQueue"] = std::move(qArr);

        std::vector<crow::json::wvalue> sArr;
        for (const auto& op : snap.recentStack) {
            crow::json::wvalue item;
            item["action"] = op.action;
            item["slotNumber"] = op.slotNumber;
            item["slotCode"] = op.slotCode;
            item["regNumber"] = op.regNumber;
            item["userName"] = op.userName;
            item["timestamp"] = op.timestamp;
            sArr.push_back(std::move(item));
        }
        res["operationStack"] = std::move(sArr);
        res["hashMap"]["vehicleCount"] = static_cast<int>(snap.hashMapVehicleCount);

        return jsonOk(std::move(res));
    });

    // ==================== STATIC FRONTEND SERVING ====================
    CROW_ROUTE(app, "/")([&frontendRoot]() {
        return serveStaticFile(frontendRoot, "index.html");
    });
    CROW_ROUTE(app, "/index.html")([&frontendRoot]() {
        return serveStaticFile(frontendRoot, "index.html");
    });
    CROW_ROUTE(app, "/dashboard.html")([&frontendRoot]() {
        return serveStaticFile(frontendRoot, "dashboard.html");
    });
    CROW_ROUTE(app, "/admin.html")([&frontendRoot]() {
        return serveStaticFile(frontendRoot, "admin.html");
    });
    CROW_ROUTE(app, "/css/<string>")([&frontendRoot](const std::string& file) {
        return serveStaticFile(frontendRoot, "css/" + file);
    });
    CROW_ROUTE(app, "/js/<string>")([&frontendRoot](const std::string& file) {
        return serveStaticFile(frontendRoot, "js/" + file);
    });

    std::cout << "[OK] Starting Smart Parking Server on http://localhost:" << cfg.serverPort << "\n";
    app.port(static_cast<uint16_t>(cfg.serverPort)).multithreaded().run();
    return 0;
}
