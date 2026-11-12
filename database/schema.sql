-- ============================================================
-- Smart Parking Management System - MySQL Database Schema
-- ============================================================

CREATE DATABASE IF NOT EXISTS smart_parking
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE smart_parking;

-- 1. Users Table (stores both normal users and admins)
CREATE TABLE IF NOT EXISTS Users (
    user_id       INT AUTO_INCREMENT PRIMARY KEY,
    full_name     VARCHAR(100) NOT NULL,
    email         VARCHAR(150) NOT NULL UNIQUE,
    phone         VARCHAR(20)  NULL,
    password_hash VARCHAR(64)  NOT NULL,
    salt          VARCHAR(32)  NOT NULL,
    role          ENUM('user', 'admin') NOT NULL DEFAULT 'user',
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- 2. Vehicles Table (indexed in C++ HashMap by reg_number)
CREATE TABLE IF NOT EXISTS Vehicles (
    vehicle_id    INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL,
    reg_number    VARCHAR(30) NOT NULL UNIQUE,
    vehicle_type  ENUM('Car', 'Bike', 'SUV', 'EV') NOT NULL DEFAULT 'Car',
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_vehicles_user
        FOREIGN KEY (user_id) REFERENCES Users(user_id) ON DELETE CASCADE
);

-- 3. Parking_Slots Table (lowest available slot_number tracked by C++ Min Heap)
CREATE TABLE IF NOT EXISTS Parking_Slots (
    slot_id       INT AUTO_INCREMENT PRIMARY KEY,
    slot_number   INT NOT NULL UNIQUE,
    slot_code     VARCHAR(20) NOT NULL UNIQUE,
    slot_type     ENUM('Car', 'Bike', 'EV') NOT NULL DEFAULT 'Car',
    is_occupied   TINYINT(1) NOT NULL DEFAULT 0,
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- 4. Bookings Table (active & completed reservations)
CREATE TABLE IF NOT EXISTS Bookings (
    booking_id    INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL,
    vehicle_id    INT NOT NULL,
    slot_id       INT NOT NULL,
    entry_time    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status        ENUM('active', 'completed', 'cancelled') NOT NULL DEFAULT 'active',
    CONSTRAINT fk_bookings_user
        FOREIGN KEY (user_id) REFERENCES Users(user_id) ON DELETE CASCADE,
    CONSTRAINT fk_bookings_vehicle
        FOREIGN KEY (vehicle_id) REFERENCES Vehicles(vehicle_id) ON DELETE CASCADE,
    CONSTRAINT fk_bookings_slot
        FOREIGN KEY (slot_id) REFERENCES Parking_Slots(slot_id) ON DELETE CASCADE
);

-- 5. Parking_History Table (entry/exit audit log)
CREATE TABLE IF NOT EXISTS Parking_History (
    history_id       INT AUTO_INCREMENT PRIMARY KEY,
    booking_id       INT NOT NULL,
    user_id          INT NOT NULL,
    vehicle_id       INT NOT NULL,
    slot_id          INT NOT NULL,
    reg_number       VARCHAR(30) NOT NULL,
    slot_number      INT NOT NULL,
    entry_time       TIMESTAMP NOT NULL,
    exit_time        TIMESTAMP NULL,
    duration_minutes INT NULL,
    status           ENUM('active', 'completed') NOT NULL DEFAULT 'active',
    CONSTRAINT fk_history_booking
        FOREIGN KEY (booking_id) REFERENCES Bookings(booking_id) ON DELETE CASCADE,
    CONSTRAINT fk_history_user
        FOREIGN KEY (user_id) REFERENCES Users(user_id) ON DELETE CASCADE,
    CONSTRAINT fk_history_vehicle
        FOREIGN KEY (vehicle_id) REFERENCES Vehicles(vehicle_id) ON DELETE CASCADE,
    CONSTRAINT fk_history_slot
        FOREIGN KEY (slot_id) REFERENCES Parking_Slots(slot_id) ON DELETE CASCADE
);

-- Initial parking slots (12 slots: P-01 to P-12)
-- No hardcoded user/admin credentials are inserted here.
-- Register users via the web UI, and create the first Admin account
-- via the "First-Time Admin Setup" button on the Admin Login tab.
INSERT IGNORE INTO Parking_Slots (slot_number, slot_code, slot_type, is_occupied) VALUES
(1,  'P-01', 'Car',  0),
(2,  'P-02', 'Car',  0),
(3,  'P-03', 'Car',  0),
(4,  'P-04', 'Car',  0),
(5,  'P-05', 'Car',  0),
(6,  'P-06', 'Car',  0),
(7,  'P-07', 'Bike', 0),
(8,  'P-08', 'Bike', 0),
(9,  'P-09', 'Bike', 0),
(10, 'P-10', 'EV',   0),
(11, 'P-11', 'EV',   0),
(12, 'P-12', 'Car',  0);
