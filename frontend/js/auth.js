document.addEventListener("DOMContentLoaded", async () => {
    // Check backend + MySQL status on load
    try {
        const health = await apiRequest("/api/health");
        if (!health.dbConnected) {
            showAlert(
                "authAlert",
                "Backend server is running, but MySQL is not connected yet. Update DB_PASSWORD in backend/config.env and import database/schema.sql.",
                "info"
            );
        }
    } catch {
        // Ignore if checking health fails
    }

    // If already logged in, verify session and redirect to the appropriate dashboard
    const existingToken = getToken();
    const existingUser = getStoredUser();
    if (existingToken && existingUser) {
        try {
            const me = await apiRequest("/api/auth/me");
            window.location.href = me.user.role === "admin" ? "admin.html" : "dashboard.html";
            return;
        } catch {
            clearSession();
        }
    }

    const tabUserLogin = document.getElementById("tabUserLogin");
    const tabRegister = document.getElementById("tabRegister");
    const tabAdminLogin = document.getElementById("tabAdminLogin");

    const userLoginForm = document.getElementById("userLoginForm");
    const registerForm = document.getElementById("registerForm");
    const adminSection = document.getElementById("adminSection");
    const adminLoginForm = document.getElementById("adminLoginForm");
    const bootstrapAdminBox = document.getElementById("bootstrapAdminBox");
    const bootstrapAdminForm = document.getElementById("bootstrapAdminForm");

    function switchTab(mode) {
        hideAlert("authAlert");
        [tabUserLogin, tabRegister, tabAdminLogin].forEach((b) => b.classList.remove("active"));
        [userLoginForm, registerForm, adminSection].forEach((el) => el.classList.add("hidden"));

        if (mode === "user") {
            tabUserLogin.classList.add("active");
            userLoginForm.classList.remove("hidden");
        } else if (mode === "register") {
            tabRegister.classList.add("active");
            registerForm.classList.remove("hidden");
        } else {
            tabAdminLogin.classList.add("active");
            adminSection.classList.remove("hidden");
            checkAdminExists();
        }
    }

    async function checkAdminExists() {
        try {
            const res = await apiRequest("/api/auth/admin-exists");
            if (!res.exists) {
                bootstrapAdminBox.classList.remove("hidden");
            } else {
                bootstrapAdminBox.classList.add("hidden");
            }
        } catch (err) {
            showAlert("authAlert", err.message, "error");
        }
    }

    tabUserLogin.addEventListener("click", () => switchTab("user"));
    tabRegister.addEventListener("click", () => switchTab("register"));
    tabAdminLogin.addEventListener("click", () => switchTab("admin"));

    userLoginForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("authAlert");
        const email = document.getElementById("userLoginEmail").value.trim();
        const password = document.getElementById("userLoginPassword").value;

        try {
            const data = await apiRequest("/api/auth/login", {
                method: "POST",
                body: { email, password, role: "user" }
            });
            saveSession(data.token, data.user);
            window.location.href = "dashboard.html";
        } catch (err) {
            showAlert("authAlert", err.message, "error");
        }
    });

    registerForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("authAlert");
        const fullName = document.getElementById("regFullName").value.trim();
        const email = document.getElementById("regEmail").value.trim();
        const phone = document.getElementById("regPhone").value.trim();
        const password = document.getElementById("regPassword").value;

        if (fullName.length < 2) {
            showAlert("authAlert", "Full name must be at least 2 characters.", "error");
            return;
        }
        if (password.length < 6) {
            showAlert("authAlert", "Password must be at least 6 characters.", "error");
            return;
        }

        try {
            const data = await apiRequest("/api/auth/register", {
                method: "POST",
                body: { fullName, email, phone, password }
            });
            registerForm.reset();
            switchTab("user");
            document.getElementById("userLoginEmail").value = email;
            showAlert("authAlert", data.message || "Registration successful! Please sign in.", "success");
        } catch (err) {
            showAlert("authAlert", err.message, "error");
        }
    });

    adminLoginForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("authAlert");
        const email = document.getElementById("adminLoginEmail").value.trim();
        const password = document.getElementById("adminLoginPassword").value;

        try {
            const data = await apiRequest("/api/auth/login", {
                method: "POST",
                body: { email, password, role: "admin" }
            });
            saveSession(data.token, data.user);
            window.location.href = "admin.html";
        } catch (err) {
            showAlert("authAlert", err.message, "error");
        }
    });

    bootstrapAdminForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        hideAlert("authAlert");
        const fullName = document.getElementById("bootAdminName").value.trim();
        const email = document.getElementById("bootAdminEmail").value.trim();
        const password = document.getElementById("bootAdminPassword").value;

        try {
            const data = await apiRequest("/api/auth/bootstrap-admin", {
                method: "POST",
                body: { fullName, email, password }
            });
            bootstrapAdminForm.reset();
            bootstrapAdminBox.classList.add("hidden");
            document.getElementById("adminLoginEmail").value = email;
            showAlert("authAlert", data.message, "success");
        } catch (err) {
            showAlert("authAlert", err.message, "error");
        }
    });
});
