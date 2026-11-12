document.addEventListener("DOMContentLoaded", async () => {
    const token = getToken();
    const user = getStoredUser();
    if (!token || !user) {
        window.location.href = "index.html";
        return;
    }
    if (user.role === "admin") {
        window.location.href = "admin.html";
        return;
    }

    document.getElementById("navUserLabel").textContent = `${user.fullName} (User)`;
    document.getElementById("btnLogout").addEventListener("click", logoutAndRedirect);

    let activeBookingId = 0;
    let selectedSlotId = 0;
    let selectedSlotCode = "";
    let availableCount = 0;

    const slotsGrid = document.getElementById("slotsGrid");
    const bookingForm = document.getElementById("bookingForm");
    const waitlistForm = document.getElementById("waitlistForm");
    const selectedSlotIdInput = document.getElementById("selectedSlotId");
    const selectedSlotDisplay = document.getElementById("selectedSlotDisplay");

    function setAutoAllocateMode() {
        selectedSlotId = 0;
        selectedSlotCode = "";
        selectedSlotIdInput.value = "0";
        selectedSlotDisplay.value = "Auto-Allocate Lowest Slot (Min Heap)";
        document.querySelectorAll(".slot-tile.selected").forEach((el) => el.classList.remove("selected"));
    }

    document.getElementById("btnResetAuto").addEventListener("click", setAutoAllocateMode);

    async function loadDashboard() {
        try {
            const [slotsData, myData] = await Promise.all([
                apiRequest("/api/slots"),
                apiRequest("/api/bookings/my")
            ]);

            const summary = slotsData.summary;
            availableCount = summary.available;

            document.getElementById("statTotal").textContent = summary.total;
            document.getElementById("statAvailable").textContent = summary.available;
            document.getElementById("statOccupied").textContent = summary.occupied;
            document.getElementById("statNextSlot").textContent =
                summary.nextLowestSlot > 0 ? `P-${String(summary.nextLowestSlot).padStart(2, "0")}` : "Full";

            renderSlots(slotsData.slots);
            renderMyBookings(myData);
        } catch (err) {
            if (err.status === 401) {
                clearSession();
                window.location.href = "index.html";
                return;
            }
            showAlert("dashAlert", err.message, "error");
        }
    }

    function renderSlots(slots) {
        if (!slots || slots.length === 0) {
            slotsGrid.innerHTML = `<p class="muted">No parking slots configured yet.</p>`;
            return;
        }

        slotsGrid.innerHTML = "";
        for (const slot of slots) {
            const tile = document.createElement("div");
            const statusClass = slot.isOccupied ? "occupied" : "available";
            const selectedClass = (!slot.isOccupied && slot.slotId === selectedSlotId) ? "selected" : "";
            const mineClass = slot.isMine ? "mine" : "";
            tile.className = `slot-tile ${statusClass} ${selectedClass} ${mineClass}`.trim();

            const statusLabel = slot.isOccupied ? (slot.isMine ? "Yours" : "Occupied") : "Available";
            const metaHtml = slot.isMine
                ? `<div class="slot-meta">${escapeHtml(slot.occupiedRegNumber)}</div>`
                : "";

            tile.innerHTML = `
                <div class="slot-code">${escapeHtml(slot.slotCode)}</div>
                <div class="slot-type">${escapeHtml(slot.slotType)}</div>
                <span class="slot-status">${statusLabel}</span>
                ${metaHtml}
            `;

            if (!slot.isOccupied) {
                tile.addEventListener("click", () => {
                    selectedSlotId = slot.slotId;
                    selectedSlotCode = slot.slotCode;
                    selectedSlotIdInput.value = String(slot.slotId);
                    selectedSlotDisplay.value = `Selected Slot: ${slot.slotCode} (${slot.slotType})`;
                    document.querySelectorAll(".slot-tile.selected").forEach((el) => el.classList.remove("selected"));
                    tile.classList.add("selected");
                });
            }

            slotsGrid.appendChild(tile);
        }
    }

    function renderMyBookings(myData) {
        const banner = document.getElementById("activeBookingBanner");
        const activeList = myData.active || [];

        if (activeList.length > 0) {
            const b = activeList[0];
            activeBookingId = b.bookingId;
            document.getElementById("activeSlotCode").textContent = b.slotCode;
            document.getElementById("activeRegNum").textContent = b.regNumber;
            document.getElementById("activeVehType").textContent = b.vehicleType;
            document.getElementById("activeEntryTime").textContent = b.entryTime;
            banner.classList.remove("hidden");
        } else {
            activeBookingId = 0;
            banner.classList.add("hidden");
        }

        // Waitlist status
        const waitBanner = document.getElementById("waitlistStatusBanner");
        if (myData.waitlistPosition && myData.waitlistPosition > 0) {
            document.getElementById("waitlistPosNum").textContent = String(myData.waitlistPosition);
            waitBanner.classList.remove("hidden");
        } else {
            waitBanner.classList.add("hidden");
        }

        // Toggle Booking Form vs Waitlist Form based on slot availability
        const titleEl = document.getElementById("bookingCardTitle");
        if (availableCount === 0 && activeBookingId === 0) {
            titleEl.textContent = "Parking Full - Join Waiting List";
            bookingForm.classList.add("hidden");
            waitlistForm.classList.remove("hidden");
        } else {
            titleEl.textContent = "Book a Parking Slot";
            bookingForm.classList.remove("hidden");
            waitlistForm.classList.add("hidden");
        }

        // Render history table
        const tbody = document.getElementById("myHistoryBody");
        const hist = myData.history || [];
        if (hist.length === 0) {
            tbody.innerHTML = `<tr><td colspan="8" class="muted">No parking bookings yet.</td></tr>`;
            return;
        }

        tbody.innerHTML = hist.map((h) => {
            const badge = h.status === "active"
                ? `<span class="badge badge-blue">Active</span>`
                : `<span class="badge badge-green">Completed</span>`;
            const dur = h.status === "completed" ? `${h.durationMinutes} min` : "-";
            const exitT = h.exitTime ? escapeHtml(h.exitTime) : "-";
            return `
                <tr>
                    <td>#${h.bookingId}</td>
                    <td><strong>${escapeHtml(h.slotCode)}</strong></td>
                    <td>${escapeHtml(h.regNumber)}</td>
                    <td>${escapeHtml(h.vehicleType)}</td>
                    <td>${escapeHtml(h.entryTime)}</td>
                    <td>${exitT}</td>
                    <td>${dur}</td>
                    <td>${badge}</td>
                </tr>
            `;
        }).join("");
    }

    bookingForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("dashAlert");

        const slotId = parseInt(selectedSlotIdInput.value, 10) || 0;
        const regNumber = document.getElementById("bookingRegNumber").value.trim();
        const vehicleType = document.getElementById("bookingVehicleType").value;

        if (regNumber.length < 4) {
            showAlert("dashAlert", "Please enter a valid vehicle registration number.", "error");
            return;
        }

        try {
            const res = await apiRequest("/api/bookings", {
                method: "POST",
                body: { slotId, regNumber, vehicleType }
            });
            showAlert("dashAlert", res.message, "success");
            bookingForm.reset();
            setAutoAllocateMode();
            await loadDashboard();
        } catch (err) {
            showAlert("dashAlert", err.message, "error");
            await loadDashboard();
        }
    });

    waitlistForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("dashAlert");
        const regNumber = document.getElementById("waitRegNumber").value.trim();
        const vehicleType = document.getElementById("waitVehicleType").value;

        try {
            const res = await apiRequest("/api/waitlist", {
                method: "POST",
                body: { regNumber, vehicleType }
            });
            showAlert("dashAlert", res.message, "success");
            waitlistForm.reset();
            await loadDashboard();
        } catch (err) {
            showAlert("dashAlert", err.message, "error");
        }
    });

    document.getElementById("btnLeaveWaitlist").addEventListener("click", async () => {
        hideAlert("dashAlert");
        try {
            const res = await apiRequest("/api/waitlist", { method: "DELETE" });
            showAlert("dashAlert", res.message, "info");
            await loadDashboard();
        } catch (err) {
            showAlert("dashAlert", err.message, "error");
        }
    });

    document.getElementById("btnReleaseActive").addEventListener("click", async () => {
        if (!activeBookingId) return;
        hideAlert("dashAlert");
        try {
            const res = await apiRequest(`/api/bookings/${activeBookingId}/release`, {
                method: "POST"
            });
            let msg = res.message;
            if (res.autoAssignedNext && res.nextBooking) {
                msg += ` (Waitlist FIFO: Automatically assigned to ${res.nextBooking.fullName} - ${res.nextBooking.regNumber})`;
            }
            showAlert("dashAlert", msg, "success");
            await loadDashboard();
        } catch (err) {
            showAlert("dashAlert", err.message, "error");
        }
    });

    document.getElementById("btnRefresh").addEventListener("click", () => {
        hideAlert("dashAlert");
        loadDashboard();
    });

    await loadDashboard();
});

