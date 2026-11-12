# Smart Parking Management System

A clean, full-stack **Smart Parking Management System** built as a college mini-project using **HTML / CSS / JavaScript**, **C++ (Crow REST API framework)**, **MySQL**, and **4 core Data Structures (DSA)** implemented directly in C++.

---

## Project Structure

```
Smart_parking/
├── .vscode/
│   ├── tasks.json              # VS Code build, test, and run tasks
│   ├── launch.json             # VS Code GDB debug configuration
│   └── c_cpp_properties.json   # IntelliSense include paths
├── database/
│   └── schema.sql              # MySQL database + 5 tables + 12 initial slots
├── backend/
│   ├── include/
│   │   ├── dsa.hpp             # Custom Queue, MinHeap, HashMap, Stack classes
│   │   ├── config.hpp          # Loads backend/config.env settings
│   │   ├── auth.hpp            # Salted SHA-256 password hashing & sessions
│   │   └── database.hpp        # Thread-safe MySQL wrapper + atomic transactions
│   ├── src/
│   │   └── main.cpp            # Crow REST API + static frontend server
│   ├── tests/
│   │   └── test_dsa.cpp        # Unit tests for DSA & Auth modules
│   ├── third_party/
│   │   ├── crow/crow_all.h     # Crow v1.3.4 single-header web framework
│   │   └── asio/               # Standalone Asio headers (no Boost needed)
│   ├── CMakeLists.txt
│   ├── build.ps1               # One-click Windows build & test script
│   ├── config.env.example      # Config template
│   └── config.env              # Local DB config (edit DB_PASSWORD here)
├── frontend/
│   ├── index.html              # User Login, Register & Admin Login/Setup
│   ├── dashboard.html          # User Dashboard (visual grid, book, release, history)
│   ├── admin.html              # Admin Dashboard (stats, slots, users, vehicles, DSA)
│   ├── css/style.css           # Responsive blue & white UI theme
│   └── js/                     # REST API client scripts (api.js, auth.js, dashboard.js, admin.js)
└── README.md
```

---

## Features

### User Features
- **Register & Login**: Salted SHA-256 password authentication stored in MySQL (`Users` table).
- **Visual Parking Grid**: Green tiles for Available slots, Red tiles for Occupied slots.
- **Book a Slot**: Either click any green slot manually, OR choose **Auto-Allocate Lowest Slot** (powered by C++ **Min Heap**).
- **Waiting List (FIFO Queue)**: If all slots are occupied, join the waiting list; when any user releases a slot, the front waiting user is automatically assigned the freed slot.
- **Release Slot & History**: Release active booking immediately (updates `Parking_Slots`, `Bookings`, and `Parking_History` in a MySQL transaction).

### Admin Features
- **First-Time Admin Setup & Login**: Zero hardcoded passwords! If no admin exists in MySQL yet, the Admin Login tab shows a one-time "Create First Admin Account" form.
- **Summary & Slot Management**: View total/available/occupied counts, add new slots, and remove unoccupied slots.
- **Users & Vehicles**: View all registered users and search vehicles by registration number using the C++ **HashMap** (`O(1)` exact lookup).
- **Active Bookings, History & Live DSA Monitor**: Inspect live **Min Heap**, **FIFO Queue**, **LIFO Stack**, and **HashMap** state.

---

## DSA Concepts Used in Backend (`backend/include/dsa.hpp`)

| Data Structure | Class Name | Purpose in System | Time Complexity |
|---|---|---|---|
| **Min Heap (Priority Queue)** | `dsa::SlotMinHeap` | Always allocates the lowest-numbered available parking slot (`P-01`, `P-02`, ...). | `insert`: $O(\log n)$, `extractMin`: $O(\log n)$ |
| **Queue (FIFO)** | `dsa::WaitingQueue` | Linked-list waiting queue when all parking slots are occupied; assigns freed slots fairly in arrival order. | `enqueue`: $O(1)$, `dequeue`: $O(1)$ |
| **HashMap (Separate Chaining)** | `dsa::VehicleHashMap` | Maps normalized vehicle registration numbers (e.g. `OD02AB1234`) to vehicle & owner details using polynomial rolling hash (`TABLE_SIZE = 101`). | Average `insert` / `find`: $O(1)$ |
| **Stack (LIFO)** | `dsa::OperationStack` | Tracks the most recent parking `ENTRY` and `EXIT` operations with latest actions on top. | `push`: $O(1)$, `pop`: $O(1)$ |

### Concurrency Safety (Preventing Simultaneous Double-Booking)
Both a C++ `std::mutex` and a MySQL transaction (`START TRANSACTION` + `SELECT ... FOR UPDATE` + `UPDATE ... WHERE is_occupied = 0`) protect slot booking so two users cannot book the same slot simultaneously.

---

## Prerequisites & Setup (Windows + VS Code)

1. **MySQL Server 8.0** (running locally on port `3306`, with `C:\Program Files\MySQL\MySQL Server 8.0\include` and `lib\libmysql.dll`).
2. **C++17 Compiler & CMake** (e.g., MinGW-w64 / WinLibs or LLVM/Clang or MSVC).
3. **VS Code** with the **C/C++** extension (optional, for IntelliSense and debugging).

### Step 1: Create the MySQL Database & Tables
Run the schema script in MySQL Workbench or Command Prompt:

```powershell
& "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe" -u root -p < database\schema.sql
```

This creates the `smart_parking` database with `Users`, `Vehicles`, `Parking_Slots` (12 initial slots), `Bookings`, and `Parking_History`.

### Step 2: Configure MySQL Password
Copy `backend/config.env.example` to `backend/config.env` (note: `backend/config.env` is in `.gitignore` so your password is never pushed to GitHub) and set your MySQL password:

```env
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=root
DB_PASSWORD=your_mysql_password_here
DB_NAME=smart_parking
SERVER_PORT=8080
FRONTEND_DIR=../frontend
```

### Step 3: Build & Run Through VS Code
1. Open the `Smart_parking` folder in VS Code.
2. Press **`Ctrl + Shift + B`** (or run Terminal -> Run Build Task -> **Build Smart Parking Backend**).
   - This runs `backend/build.ps1`, compiles `smart_parking_server.exe` and `test_dsa.exe`, copies `libmysql.dll`, and runs the DSA unit tests.
3. Run Terminal -> Run Task -> **Run Smart Parking Server** (or run `backend\build\smart_parking_server.exe` from the `backend` folder).
4. Open your browser to: **http://localhost:8080**

> **Note on Windows 11 Smart App Control**: If Windows Smart App Control is enabled (`On`) on your PC, Windows may block running newly compiled unsigned `.exe` files (`smart_parking_server.exe` / `test_dsa.exe`). If you see an Application Control block, sign the binaries with a local trusted code-signing certificate, compile with Visual Studio (Developer Mode / trusted toolchain), or adjust Windows Security -> App & browser control -> Smart App Control settings.

### Step 4: First Demo Walkthrough
1. Click the **Admin Login** tab -> create the first Admin account under "First-Time Admin Setup" -> sign in to view the Admin Dashboard.
2. Open a second browser/incognito window -> register a User account -> log in -> book a slot (try both clicking a green slot and Auto-Allocate).
3. Release the slot and check **My Parking History** and the Admin **DSA Monitor** tab!

---

## REST API Endpoints

- `GET /api/health` — Server & MySQL health check
- `GET /api/auth/admin-exists` — Checks whether an admin exists
- `POST /api/auth/register` — Register a new user
- `POST /api/auth/bootstrap-admin` — Create the first admin (only when 0 admins exist)
- `POST /api/auth/login` — Authenticate user or admin
- `GET /api/slots` — List all slots & occupancy summary
- `POST /api/bookings` — Book specific slot (`slotId > 0`) or auto-allocate via Min Heap (`slotId = 0`)
- `POST /api/bookings/:id/release` — Release active slot (and auto-assign FIFO waitlist user if waiting)
- `GET /api/bookings/my` — User's active booking, waitlist position, and history
- `POST /api/waitlist` / `DELETE /api/waitlist` — Join or leave FIFO waiting queue
- `POST /api/admin/slots` / `DELETE /api/admin/slots/:id` — Add or remove parking slots
- `GET /api/admin/users` — List all registered users
- `GET /api/admin/vehicles?q=...` — List/search vehicles via HashMap
- `GET /api/admin/bookings/active` & `GET /api/admin/history` — View active bookings & history
- `GET /api/admin/dsa` — Inspect live Min Heap, Queue, Stack, and HashMap state

