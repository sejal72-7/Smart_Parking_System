document.addEventListener("DOMContentLoaded", async () => {
    const token = getToken();
    const user = getStoredUser();
    if (!token || !user) {
        window.location.href = "index.html";
        return;
    }
    if (user.role !== "admin") {
        window.location.href = "dashboard.html";
        return;
    }

    document.getElementById("navAdminLabel").textContent = `${user.fullName} (Admin)`;
    document.getElementById("btnLogoutAdmin").addEventListener("click", logoutAndRedirect);

    // Tab switching
    const tabButtons = document.querySelectorAll(".admin-tabs .tab-btn");
    const panels = document.querySelectorAll(".admin-panel");

    tabButtons.forEach((btn) => {
        btn.addEventListener("click", () => {
            const targetId = btn.getAttribute("data-panel");
            tabButtons.forEach((b) => b.classList.remove("active"));
            panels.forEach((p) => p.classList.add("hidden"));
            btn.classList.add("active");
            document.getElementById(targetId).classList.remove("hidden");
        });
    });

    async function loadAllAdminData() {
        try {
            const [slotsData, usersData, vehData, activeData, histData, dsaData] = await Promise.all([
                apiRequest("/api/slots"),
                apiRequest("/api/admin/users"),
                apiRequest("/api/admin/vehicles"),
                apiRequest("/api/admin/bookings/active"),
                apiRequest("/api/admin/history"),
                apiRequest("/api/admin/dsa")
            ]);

            const s = slotsData.summary;
            document.getElementById("admStatTotal").textContent = s.total;
            document.getElementById("admStatAvailable").textContent = s.available;
            document.getElementById("admStatOccupied").textContent = s.occupied;
            document.getElementById("admStatWaitlist").textContent = s.waitlistCount || 0;

            renderAdminSlots(slotsData.slots);
            renderUsers(usersData.users);
            renderVehicles(vehData.vehicles, "");
            renderActiveBookings(activeData.bookings);
            renderHistory(histData.history);
            renderDsa(dsaData);
        } catch (err) {
            if (err.status === 401 || err.status === 403) {
                clearSession();
                window.location.href = "index.html";
                return;
            }
            showAlert("adminAlert", err.message, "error");
        }
    }

    function renderAdminSlots(slots) {
        const grid = document.getElementById("adminSlotsGrid");
        if (!slots || slots.length === 0) {
            grid.innerHTML = `<p class="muted">No parking slots configured.</p>`;
            return;
        }
        grid.innerHTML = "";

        for (const slot of slots) {
            const tile = document.createElement("div");
            tile.className = `slot-tile ${slot.isOccupied ? "occupied" : "available"}`;

            const meta = slot.isOccupied
                ? `<div class="slot-meta">${escapeHtml(slot.occupiedRegNumber)}<br>${escapeHtml(slot.occupiedByName)}</div>`
                : `<button class="btn btn-danger btn-sm slot-remove-btn" type="button" data-slot-id="${slot.slotId}">Remove</button>`;

            tile.innerHTML = `
                <div class="slot-code">${escapeHtml(slot.slotCode)}</div>
                <div class="slot-type">#${slot.slotNumber} &bull; ${escapeHtml(slot.slotType)}</div>
                <span class="slot-status">${slot.isOccupied ? "Occupied" : "Available"}</span>
                ${meta}
            `;

            const rmBtn = tile.querySelector(".slot-remove-btn");
            if (rmBtn) {
                rmBtn.addEventListener("click", async () => {
                    hideAlert("adminAlert");
                    try {
                        const res = await apiRequest(`/api/admin/slots/${slot.slotId}`, { method: "DELETE" });
                        showAlert("adminAlert", res.message, "success");
                        await loadAllAdminData();
                    } catch (err) {
                        showAlert("adminAlert", err.message, "error");
                    }
                });
            }

            grid.appendChild(tile);
        }
    }

    function renderUsers(users) {
        const body = document.getElementById("adminUsersBody");
        if (!users || users.length === 0) {
            body.innerHTML = `<tr><td colspan="6" class="muted">No users registered.</td></tr>`;
            return;
        }
        body.innerHTML = users.map((u) => `
            <tr>
                <td>#${u.userId}</td>
                <td><strong>${escapeHtml(u.fullName)}</strong></td>
                <td>${escapeHtml(u.email)}</td>
                <td>${escapeHtml(u.phone || "-")}</td>
                <td><span class="badge ${u.role === "admin" ? "badge-amber" : "badge-blue"}">${escapeHtml(u.role)}</span></td>
                <td>${escapeHtml(u.createdAt)}</td>
            </tr>
        `).join("");
    }

    function renderVehicles(vehicles, metaMsg) {
        document.getElementById("vehicleSearchMeta").textContent = metaMsg;
        const body = document.getElementById("adminVehiclesBody");
        if (!vehicles || vehicles.length === 0) {
            body.innerHTML = `<tr><td colspan="6" class="muted">No matching vehicles found.</td></tr>`;
            return;
        }
        body.innerHTML = vehicles.map((v) => `
            <tr>
                <td>#${v.vehicleId}</td>
                <td><strong>${escapeHtml(v.regNumber)}</strong></td>
                <td>${escapeHtml(v.vehicleType)}</td>
                <td>${escapeHtml(v.ownerName)}</td>
                <td>${escapeHtml(v.ownerEmail)}</td>
                <td>${v.activeSlotNumber > 0
                    ? `<span class="badge badge-red">Slot #${v.activeSlotNumber}</span>`
                    : `<span class="badge badge-green">Not Parked</span>`}</td>
            </tr>
        `).join("");
    }

    function renderActiveBookings(bookings) {
        const body = document.getElementById("adminActiveBody");
        if (!bookings || bookings.length === 0) {
            body.innerHTML = `<tr><td colspan="7" class="muted">No active bookings right now.</td></tr>`;
            return;
        }
        body.innerHTML = bookings.map((b) => `
            <tr>
                <td>#${b.bookingId}</td>
                <td><strong>${escapeHtml(b.slotCode)}</strong></td>
                <td>${escapeHtml(b.fullName)}</td>
                <td>${escapeHtml(b.regNumber)}</td>
                <td>${escapeHtml(b.vehicleType)}</td>
                <td>${escapeHtml(b.entryTime)}</td>
                <td><button class="btn btn-danger btn-sm btn-force-release" data-id="${b.bookingId}" type="button">Release</button></td>
            </tr>
        `).join("");

        body.querySelectorAll(".btn-force-release").forEach((btn) => {
            btn.addEventListener("click", async () => {
                hideAlert("adminAlert");
                const id = btn.getAttribute("data-id");
                try {
                    const res = await apiRequest(`/api/bookings/${id}/release`, { method: "POST" });
                    showAlert("adminAlert", res.message, "success");
                    await loadAllAdminData();
                } catch (err) {
                    showAlert("adminAlert", err.message, "error");
                }
            });
        });
    }

    function renderHistory(history) {
        const body = document.getElementById("adminHistoryBody");
        if (!history || history.length === 0) {
            body.innerHTML = `<tr><td colspan="8" class="muted">No parking history yet.</td></tr>`;
            return;
        }
        body.innerHTML = history.map((h) => {
            const badge = h.status === "active"
                ? `<span class="badge badge-blue">Active</span>`
                : `<span class="badge badge-green">Completed</span>`;
            return `
                <tr>
                    <td>#${h.bookingId}</td>
                    <td><strong>${escapeHtml(h.slotCode)}</strong></td>
                    <td>${escapeHtml(h.fullName)}</td>
                    <td>${escapeHtml(h.regNumber)}</td>
                    <td>${escapeHtml(h.entryTime)}</td>
                    <td>${h.exitTime ? escapeHtml(h.exitTime) : "-"}</td>
                    <td>${h.status === "completed" ? `${h.durationMinutes} min` : "-"}</td>
                    <td>${badge}</td>
                </tr>
            `;
        }).join("");
    }

    function renderDsa(dsa) {
        const minSlot = dsa.minHeap?.nextLowestSlot || 0;
        document.getElementById("dsaNextMinSlot").textContent =
            minSlot > 0 ? `P-${String(minSlot).padStart(2, "0")} (#${minSlot})` : "None (Lot Full)";

        const heapPills = document.getElementById("dsaHeapPills");
        const heapArr = dsa.minHeap?.heapArray || [];
        heapPills.innerHTML = heapArr.length > 0
            ? heapArr.map((n) => `<span class="heap-pill">#${n}</span>`).join("")
            : `<span class="muted">Heap is empty (all slots occupied).</span>`;

        const qList = document.getElementById("dsaQueueList");
        const queue = dsa.waitingQueue || [];
        qList.innerHTML = queue.length > 0
            ? queue.map((w) => `
                <li class="dsa-item">
                    <span><strong>#${w.position}</strong> ${escapeHtml(w.fullName)} &bull; ${escapeHtml(w.regNumber)} (${escapeHtml(w.vehicleType)})</span>
                    <span class="muted">${escapeHtml(w.requestedAt)}</span>
                </li>`).join("")
            : `<li class="muted">Waiting list is empty.</li>`;

        document.getElementById("dsaHashCount").textContent = String(dsa.hashMap?.vehicleCount ?? 0);

        const sList = document.getElementById("dsaStackList");
        const stack = dsa.operationStack || [];
        sList.innerHTML = stack.length > 0
            ? stack.map((op) => `
                <li class="dsa-item">
                    <span>
                        <span class="badge ${op.action === "ENTRY" ? "badge-green" : "badge-red"}">${escapeHtml(op.action)}</span>
                        <strong>${escapeHtml(op.slotCode)}</strong> &bull; ${escapeHtml(op.regNumber)} (${escapeHtml(op.userName)})
                    </span>
                    <span class="muted">${escapeHtml(op.timestamp)}</span>
                </li>`).join("")
            : `<li class="muted">No operations recorded yet.</li>`;
    }

    // Add Slot Form
    document.getElementById("addSlotForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("adminAlert");
        const slotNumber = parseInt(document.getElementById("newSlotNumber").value, 10);
        const slotCode = document.getElementById("newSlotCode").value.trim();
        const slotType = document.getElementById("newSlotType").value;

        try {
            const res = await apiRequest("/api/admin/slots", {
                method: "POST",
                body: { slotNumber, slotCode, slotType }
            });
            showAlert("adminAlert", res.message, "success");
            e.target.reset();
            await loadAllAdminData();
        } catch (err) {
            showAlert("adminAlert", err.message, "error");
        }
    });

    // HashMap Vehicle Search
    document.getElementById("vehicleSearchForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        const q = document.getElementById("vehicleSearchInput").value.trim();
        try {
            const res = await apiRequest(`/api/admin/vehicles?q=${encodeURIComponent(q)}`);
            const meta = q
                ? (res.exactHashHit
                    ? `O(1) HashMap exact match found for "${q}".`
                    : `Showing ${res.vehicles.length} matching vehicle(s) for "${q}".`)
                : "";
            renderVehicles(res.vehicles, meta);
        } catch (err) {
            showAlert("adminAlert", err.message, "error");
        }
    });

    document.getElementById("btnResetVehicleSearch").addEventListener("click", async () => {
        document.getElementById("vehicleSearchInput").value = "";
        const res = await apiRequest("/api/admin/vehicles");
        renderVehicles(res.vehicles, "");
    });

    document.getElementById("btnRefreshAdmin").addEventListener("click", () => {
        hideAlert("adminAlert");
        loadAllAdminData();
    });

    await loadAllAdminData();
});

