// Shared REST API helper and session storage utilities

const API_BASE = window.location.origin.startsWith("http")
    ? ""
    : "http://localhost:8080";

function getToken() {
    return localStorage.getItem("sp_token") || "";
}

function getStoredUser() {
    const raw = localStorage.getItem("sp_user");
    if (!raw) return null;
    try {
        return JSON.parse(raw);
    } catch {
        return null;
    }
}

function saveSession(token, user) {
    localStorage.setItem("sp_token", token);
    localStorage.setItem("sp_user", JSON.stringify(user));
}

function clearSession() {
    localStorage.removeItem("sp_token");
    localStorage.removeItem("sp_user");
}

async function apiRequest(path, options = {}) {
    const token = getToken();
    const headers = {
        "Content-Type": "application/json",
        ...(options.headers || {})
    };
    if (token) {
        headers["Authorization"] = `Bearer ${token}`;
    }

    const response = await fetch(`${API_BASE}${path}`, {
        method: options.method || "GET",
        headers,
        body: options.body ? JSON.stringify(options.body) : undefined
    });

    let data = {};
    try {
        data = await response.json();
    } catch {
        data = { ok: false, error: "Invalid server response." };
    }

    if (!response.ok) {
        const err = new Error(data.error || `Request failed (${response.status})`);
        err.status = response.status;
        throw err;
    }
    return data;
}

function showAlert(elementId, message, type = "error") {
    const el = document.getElementById(elementId);
    if (!el) return;
    el.textContent = message;
    el.className = `alert show alert-${type}`;
}

function hideAlert(elementId) {
    const el = document.getElementById(elementId);
    if (!el) return;
    el.className = "alert";
    el.textContent = "";
}

function escapeHtml(str) {
    return String(str ?? "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#39;");
}

async function logoutAndRedirect() {
    try {
        await apiRequest("/api/auth/logout", { method: "POST" });
    } catch {
        // Ignore network errors on logout
    }
    clearSession();
    window.location.href = "index.html";
}

