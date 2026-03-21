/* ==========================================
   FACEGUARD IoT — Main JavaScript Controller
   ========================================== */

const API_BASE = "";  // empty = same origin (Flask serves frontend)

// ===== STATE =====
let allLogs = [];
let allUsers = [];
let currentFilter = "all";
let capturedImage = null;
let stream = null;
let currentDeleteTarget = null;

// ===== INIT =====
document.addEventListener("DOMContentLoaded", () => {
  initNavigation();
  loadStats();
  loadRecentLogs();
  loadUsers();
  loadLogs();
  startAutoRefresh();
  connectSSE();       // ← real-time push từ AI server
});

// ===== SSE — REAL-TIME UPDATE =====
let sseSource = null;

function connectSSE() {
  if (sseSource) sseSource.close();

  sseSource = new EventSource("/events");

  sseSource.addEventListener("connected", () => {
    console.log("[SSE] Kết nối real-time thành công ✅");
    document.getElementById("server-status-dot").className = "status-indicator online";
    document.getElementById("server-status-text").textContent = "Live · Real-time";
  });

  // ── Nhận diện khuôn mặt từ AI ──
  sseSource.addEventListener("recognize", (e) => {
    const data = JSON.parse(e.data);
    const { user, status, time } = data;

    console.log("[SSE] Nhận diện:", data);

    // 1. Toast notification ngay lập tức
    if (status === "granted") {
      showToast("success", `🔓 <b>${user}</b> được vào lúc ${time}`, 6000);
    } else {
      showToast("warning", `⚠️ Người lạ phát hiện lúc ${time}`, 5000);
    }

    // 2. Cập nhật stats + logs ngay không cần đợi auto-refresh
    loadStats();
    loadRecentLogs();
    loadLogs();

    // 3. Flash hiệu ứng trên dashboard nếu đang ở trang dashboard
    const activePage = document.querySelector(".page.active");
    if (activePage && activePage.id === "page-dashboard") {
      flashDashboard(status);
    }
  });

  sseSource.onerror = () => {
    console.warn("[SSE] Mất kết nối, thử lại sau 3s...");
    document.getElementById("server-status-dot").className = "status-indicator offline";
    document.getElementById("server-status-text").textContent = "Mất kết nối";
    sseSource.close();
    setTimeout(connectSSE, 3000);  // tự reconnect
  };
}

// Flash hiệu ứng khi có người quét
function flashDashboard(status) {
  const color = status === "granted"
    ? "rgba(74, 222, 128, 0.08)"
    : "rgba(248, 113, 113, 0.08)";

  const dashboard = document.getElementById("page-dashboard");
  dashboard.style.transition = "background 0.2s";
  dashboard.style.background = color;
  setTimeout(() => {
    dashboard.style.background = "";
  }, 800);
}


// ===== NAVIGATION =====
function initNavigation() {
  document.querySelectorAll(".nav-item").forEach(item => {
    item.addEventListener("click", () => {
      const page = item.getAttribute("data-page");
      switchPage(page);
      // Close sidebar on mobile
      if (window.innerWidth <= 768) {
        document.getElementById("sidebar").classList.remove("open");
      }
    });
  });
}

function switchPage(pageName) {
  // Hide all pages
  document.querySelectorAll(".page").forEach(p => p.classList.remove("active"));
  document.querySelectorAll(".nav-item").forEach(n => n.classList.remove("active"));

  // Show target page
  const targetPage = document.getElementById("page-" + pageName);
  const targetNav = document.getElementById("nav-" + pageName);

  if (targetPage) targetPage.classList.add("active");
  if (targetNav) targetNav.classList.add("active");

  // Update topbar title
  const titles = {
    dashboard: "Dashboard",
    register: "Đăng ký khuôn mặt",
    users: "Người dùng",
    logs: "Lịch sử truy cập"
  };
  document.getElementById("page-title").textContent = titles[pageName] || pageName;

  // Page-specific actions
  if (pageName === "users") loadUsers();
  if (pageName === "logs") loadLogs();
  if (pageName === "dashboard") { loadStats(); loadRecentLogs(); }
}

function toggleSidebar() {
  document.getElementById("sidebar").classList.toggle("open");
}

// ===== AUTO REFRESH =====
function startAutoRefresh() {
  setInterval(() => {
    loadStats();
    loadRecentLogs();
    // quietly update badges
  }, 10000); // every 10s
}

function refreshAll() {
  loadStats();
  loadRecentLogs();
  loadUsers();
  loadLogs();
  showToast("success", "🔄 Đã làm mới dữ liệu!");
}

// ===== API FUNCTIONS =====
async function apiFetch(url, options = {}) {
  try {
    const res = await fetch(API_BASE + url, {
      headers: { "Content-Type": "application/json" },
      ...options
    });
    return await res.json();
  } catch (err) {
    console.error("API Error:", err);
    return null;
  }
}

// ===== STATS =====
async function loadStats() {
  const data = await apiFetch("/stats");
  if (!data) return;

  animateNumber("total-users", data.total_users || 0);
  animateNumber("total-access", data.total_access || 0);
  animateNumber("total-granted", data.granted || 0);
  animateNumber("total-denied", data.denied || 0);

  // Update badges
  document.getElementById("users-badge").textContent = data.total_users || 0;
  document.getElementById("logs-badge").textContent = data.total_access || 0;

  // Update server status
  document.getElementById("server-status-dot").className = "status-indicator online";
  document.getElementById("server-status-text").textContent = "Máy chủ online";
}

function animateNumber(id, target) {
  const el = document.getElementById(id);
  if (!el) return;
  const current = parseInt(el.textContent) || 0;
  if (current === target) return;

  const diff = target - current;
  const steps = 20;
  const step = diff / steps;
  let count = current;
  let i = 0;

  const timer = setInterval(() => {
    count += step;
    i++;
    el.textContent = Math.round(count);
    el.classList.add("counting");
    setTimeout(() => el.classList.remove("counting"), 200);
    if (i >= steps) {
      clearInterval(timer);
      el.textContent = target;
    }
  }, 20);
}

// ===== RECENT LOGS (Dashboard) =====
async function loadRecentLogs() {
  const data = await apiFetch("/logs");
  if (!data) return;

  const container = document.getElementById("recent-logs");
  const recent = data.slice(0, 6);

  if (recent.length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">📋</div>
        <p>Chưa có hoạt động nào</p>
      </div>`;
    return;
  }

  container.innerHTML = recent.map(log => createRecentLogHTML(log)).join("");
}

function createRecentLogHTML(log) {
  const badgeClass = getBadgeClass(log.status);
  const badgeText = getBadgeText(log.status);
  const initial = (log.user || "?")[0].toUpperCase();

  return `
    <div class="recent-log-item">
      <div class="log-avatar">${initial}</div>
      <div class="log-info">
        <div class="log-name">${escapeHtml(log.user || "Unknown")}</div>
        <div class="log-time">${formatTime(log.time)}</div>
      </div>
      <span class="badge ${badgeClass}">${badgeText}</span>
    </div>`;
}

// ===== USERS =====
async function loadUsers() {
  const data = await apiFetch("/users");
  if (!data) return;
  allUsers = data;
  renderUsers(data);
}

function renderUsers(users) {
  const grid = document.getElementById("users-grid");

  if (users.length === 0) {
    grid.innerHTML = `
      <div class="empty-state" style="grid-column: 1/-1;">
        <div class="empty-icon">👥</div>
        <p>Chưa có người dùng nào</p>
        <button class="btn btn-sm btn-primary" onclick="switchPage('register')">+ Đăng ký ngay</button>
      </div>`;
    return;
  }

  grid.innerHTML = users.map(user => createUserCardHTML(user)).join("");
}

function createUserCardHTML(user) {
  const initial = (user.name || "?")[0].toUpperCase();
  const lastAccess = user.last_access || "Chưa truy cập";

  let avatarHTML = initial;
  if (user.avatar && user.avatar.length > 50) {
    avatarHTML = `<img src="data:image/jpeg;base64,${user.avatar}" alt="${escapeHtml(user.name)}" onerror="this.style.display='none'" />`;
  }

  return `
    <div class="user-card" id="uc-${encodeURIComponent(user.name)}">
      <div class="user-avatar">
        ${avatarHTML}
      </div>
      <div class="user-name">${escapeHtml(user.name)}</div>
      <div class="user-reg-date">📅 ${formatDateShort(user.registered_at)}</div>
      <div class="user-stats">
        <div class="user-stat">
          <div class="user-stat-value">${user.access_count || 0}</div>
          <div class="user-stat-label">Lần vào</div>
        </div>
        <div class="user-stat">
          <div class="user-stat-value" style="font-size:11px;color:var(--text-muted)">${formatTimeShort(user.last_access)}</div>
          <div class="user-stat-label">Lần cuối</div>
        </div>
      </div>
      <div class="user-card-actions">
        <button class="btn btn-sm btn-outline" onclick="viewUserLogs('${escapeHtml(user.name)}')">
          📋 Log
        </button>
        <button class="btn btn-sm btn-danger" onclick="promptDeleteUser('${escapeHtml(user.name)}')">
          🗑 Xóa
        </button>
      </div>
    </div>`;
}

function filterUsers() {
  const query = document.getElementById("user-search").value.toLowerCase();
  const filtered = allUsers.filter(u => u.name.toLowerCase().includes(query));
  renderUsers(filtered);
}

function viewUserLogs(name) {
  switchPage("logs");
  setTimeout(() => {
    const tbody = document.getElementById("logs-tbody");
    const filtered = allLogs.filter(l => l.user === name);
    renderLogsTable(filtered);
  }, 100);
}

// ===== DELETE USER =====
function promptDeleteUser(name) {
  currentDeleteTarget = name;
  document.getElementById("delete-name").textContent = name;
  document.getElementById("delete-modal").classList.add("show");
}

function closeDeleteModal() {
  document.getElementById("delete-modal").classList.remove("show");
  currentDeleteTarget = null;
}

async function confirmDelete() {
  if (!currentDeleteTarget) return;
  const name = currentDeleteTarget;
  closeDeleteModal();

  const data = await apiFetch(`/users/${encodeURIComponent(name)}`, { method: "DELETE" });

  if (data && data.status === "success") {
    showToast("success", `✅ Đã xóa ${name}`);
    loadUsers();
    loadStats();
  } else {
    showToast("error", "❌ Xóa thất bại");
  }
}

// ===== LOGS =====
async function loadLogs() {
  const data = await apiFetch("/logs");
  if (!data) return;
  allLogs = data;
  applyLogFilter();
}

function filterLogs(filter, btn) {
  currentFilter = filter;
  document.querySelectorAll(".filter-btn").forEach(b => b.classList.remove("active"));
  btn.classList.add("active");
  applyLogFilter();
}

function applyLogFilter() {
  let filtered = allLogs;
  if (currentFilter !== "all") {
    if (currentFilter === "register") {
      filtered = allLogs.filter(l => l.type === "register");
    } else {
      filtered = allLogs.filter(l => l.status === currentFilter);
    }
  }
  renderLogsTable(filtered);
}

function renderLogsTable(logs) {
  const tbody = document.getElementById("logs-tbody");

  if (logs.length === 0) {
    tbody.innerHTML = `
      <tr>
        <td colspan="5">
          <div class="empty-state">
            <div class="empty-icon">📋</div>
            <p>Không có dữ liệu</p>
          </div>
        </td>
      </tr>`;
    return;
  }

  tbody.innerHTML = logs.map((log, i) => {
    const badgeClass = getBadgeClass(log.status);
    const badgeText = getBadgeText(log.status);
    const initial = (log.user || "?")[0].toUpperCase();
    const typeIcon = getTypeIcon(log.type);

    return `
      <tr class="row-enter">
        <td class="log-num">${i + 1}</td>
        <td>
          <div class="log-user-cell">
            <div class="icon">${initial}</div>
            <span>${escapeHtml(log.user || "Unknown")}</span>
          </div>
        </td>
        <td><span class="badge ${badgeClass}">${badgeText}</span></td>
        <td class="log-type-cell">${typeIcon} ${log.type || "access"}</td>
        <td class="log-time-cell">${formatTime(log.time)}</td>
      </tr>`;
  }).join("");
}

async function clearLogs() {
  if (!confirm("Xóa toàn bộ lịch sử? Hành động không thể hoàn tác!")) return;
  const data = await apiFetch("/logs/clear", { method: "DELETE" });
  if (data && data.status === "success") {
    allLogs = [];
    renderLogsTable([]);
    showToast("success", "🗑 Đã xóa toàn bộ log");
    loadStats();
  }
}

// ===== CAMERA =====
async function startCamera() {
  try {
    stream = await navigator.mediaDevices.getUserMedia({
      video: { width: 640, height: 480, facingMode: "user" },
      audio: false
    });

    const video = document.getElementById("webcam");
    video.srcObject = stream;
    video.style.display = "block";
    document.getElementById("camera-placeholder").style.display = "none";
    document.getElementById("face-overlay").style.display = "block";
    document.getElementById("camera-actions").style.display = "block";
    document.getElementById("start-cam-btn").style.display = "none";
    document.getElementById("stop-cam-btn").style.display = "inline-flex";

    showToast("success", "📷 Camera đã bật");
  } catch (err) {
    showToast("error", "❌ Không thể truy cập camera: " + err.message);
  }
}

function stopCamera() {
  if (stream) {
    stream.getTracks().forEach(t => t.stop());
    stream = null;
  }
  document.getElementById("webcam").style.display = "none";
  document.getElementById("camera-placeholder").style.display = "flex";
  document.getElementById("face-overlay").style.display = "none";
  document.getElementById("camera-actions").style.display = "none";
  document.getElementById("start-cam-btn").style.display = "inline-flex";
  document.getElementById("stop-cam-btn").style.display = "none";

  showToast("info", "📷 Camera đã tắt");
}

function capturePhoto() {
  const video = document.getElementById("webcam");
  const canvas = document.getElementById("snapshot-canvas");
  const ctx = canvas.getContext("2d");

  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  ctx.drawImage(video, 0, 0);

  // Get base64
  capturedImage = canvas.toDataURL("image/jpeg", 0.85).split(",")[1];

  // Show preview
  document.getElementById("snapshot-img").src = canvas.toDataURL("image/jpeg", 0.85);
  document.getElementById("snapshot-preview").style.display = "block";

  // Flash effect
  const overlay = document.getElementById("face-overlay");
  overlay.style.background = "rgba(255,255,255,0.3)";
  setTimeout(() => { overlay.style.background = "none"; }, 200);

  // Update step
  document.getElementById("step-photo").classList.add("done");
  document.getElementById("step-photo").querySelector(".step-icon").textContent = "✓";

  showToast("success", "📸 Chụp ảnh thành công!");
}

function retakePhoto() {
  capturedImage = null;
  document.getElementById("snapshot-preview").style.display = "none";
  document.getElementById("step-photo").classList.remove("done");
  document.getElementById("step-photo").querySelector(".step-icon").textContent = "2";
}

// ===== REGISTER USER =====
async function registerUser() {
  const name = document.getElementById("reg-name").value.trim();

  if (!name) {
    showToast("error", "⚠️ Vui lòng nhập tên người dùng");
    document.getElementById("reg-name").focus();
    return;
  }

  if (!capturedImage) {
    showToast("warning", "📷 Vui lòng chụp ảnh khuôn mặt trước");
    return;
  }

  const btn = document.getElementById("register-btn");
  btn.disabled = true;
  btn.innerHTML = `<span class="spinner"></span> Đang đăng ký...`;

  const data = await apiFetch("/register", {
    method: "POST",
    body: JSON.stringify({ name, image: capturedImage })
  });

  btn.disabled = false;
  btn.innerHTML = `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="20" height="20">
      <path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/>
      <circle cx="9" cy="7" r="4"/>
      <line x1="19" y1="8" x2="19" y2="14"/>
      <line x1="22" y1="11" x2="16" y2="11"/>
    </svg>
    Đăng ký`;

  if (data && data.status === "success") {
    showToast("success", `✅ ${data.message}`);

    // Complete step
    document.getElementById("step-done").classList.add("done");
    document.getElementById("step-done").querySelector(".step-icon").textContent = "✓";

    // Reset form after 2s
    setTimeout(() => {
      document.getElementById("reg-name").value = "";
      capturedImage = null;
      document.getElementById("snapshot-preview").style.display = "none";
      document.getElementById("step-photo").classList.remove("done");
      document.getElementById("step-done").classList.remove("done");
      document.getElementById("step-photo").querySelector(".step-icon").textContent = "2";
      document.getElementById("step-done").querySelector(".step-icon").textContent = "3";
    }, 2000);

    loadStats();
  } else {
    const msg = data?.message || "Đăng ký thất bại";
    showToast("error", `❌ ${msg}`);
  }
}

// ===== MANUAL UNLOCK =====
function manualUnlock() {
  document.getElementById("unlock-modal").classList.add("show");
}

function closeModal() {
  document.getElementById("unlock-modal").classList.remove("show");
}

async function confirmUnlock() {
  closeModal();
  const data = await apiFetch("/unlock", { method: "POST" });

  if (data && data.status === "success") {
    showToast("success", "🔓 Đã gửi lệnh mở khóa đến STM32!");
    // Add to recent logs instantly
    loadRecentLogs();
    loadLogs();
  } else {
    showToast("error", "❌ Không thể gửi lệnh mở khóa");
  }
}

// ===== TOAST NOTIFICATION =====
function showToast(type, message, duration = 4000) {
  const container = document.getElementById("toast-container");
  const icons = {
    success: "✅",
    error: "❌",
    warning: "⚠️",
    info: "ℹ️"
  };

  const toast = document.createElement("div");
  toast.className = `toast toast-${type}`;
  toast.innerHTML = `
    <span class="toast-icon">${icons[type] || "ℹ️"}</span>
    <span class="toast-msg">${message}</span>
  `;

  container.appendChild(toast);

  setTimeout(() => {
    toast.classList.add("out");
    setTimeout(() => toast.remove(), 300);
  }, duration);
}

// ===== HELPER FUNCTIONS =====
function getBadgeClass(status) {
  if (status === "granted") return "badge-granted";
  if (status === "denied") return "badge-denied";
  if (status === "registered") return "badge-register";
  if (status === "manual_unlock") return "badge-manual";
  return "badge-register";
}

function getBadgeText(status) {
  if (status === "granted") return "✓ Granted";
  if (status === "denied") return "✗ Denied";
  if (status === "registered") return "📋 Đăng ký";
  return status || "N/A";
}

function getTypeIcon(type) {
  const icons = {
    access: "🚪",
    register: "📝",
    manual_unlock: "🔓"
  };
  return icons[type] || "📌";
}

function formatTime(timeStr) {
  if (!timeStr) return "N/A";
  return timeStr;
}

function formatDateShort(dateStr) {
  if (!dateStr) return "N/A";
  return dateStr.split(" ")[0];
}

function formatTimeShort(dateStr) {
  if (!dateStr || dateStr === "Chưa truy cập") return "—";
  const parts = dateStr.split(" ");
  return parts[1] || parts[0];
}

function escapeHtml(text) {
  const div = document.createElement("div");
  div.appendChild(document.createTextNode(text || ""));
  return div.innerHTML;
}

// ===== KEYBOARD SHORTCUTS =====
document.addEventListener("keydown", e => {
  if (e.key === "Escape") {
    closeModal();
    closeDeleteModal();
    if (window.innerWidth <= 768) {
      document.getElementById("sidebar").classList.remove("open");
    }
  }
  if (e.key === "F5") {
    e.preventDefault();
    refreshAll();
  }
});

// Close modal on overlay click
document.getElementById("unlock-modal").addEventListener("click", function(e) {
  if (e.target === this) closeModal();
});
document.getElementById("delete-modal").addEventListener("click", function(e) {
  if (e.target === this) closeDeleteModal();
});
document.getElementById("emergency-modal").addEventListener("click", function(e) {
  if (e.target === this) closeEmergency();
});

// Make sure modals are hidden on init
document.getElementById("unlock-modal").classList.remove("show");
document.getElementById("delete-modal").classList.remove("show");

// ===== EMERGENCY UNLOCK =====
const EMERGENCY_COUNTDOWN = 5;        // giây
const CIRCUMFERENCE = 2 * Math.PI * 33;  // 207.3px
let emergencyTimer  = null;
let emergencyCount  = EMERGENCY_COUNTDOWN;

function openEmergency() {
  emergencyCount = EMERGENCY_COUNTDOWN;

  // Reset UI
  const numEl    = document.getElementById("countdown-number");
  const circleEl = document.getElementById("countdown-circle");
  const confirmBtn = document.getElementById("emergency-confirm-btn");
  const cancelBtn  = document.getElementById("emergency-cancel-btn");

  numEl.textContent = emergencyCount;
  circleEl.style.strokeDashoffset = 0;
  confirmBtn.disabled = false;
  cancelBtn.disabled  = false;

  document.getElementById("emergency-modal").classList.add("show");

  // Start countdown
  clearInterval(emergencyTimer);
  emergencyTimer = setInterval(() => {
    emergencyCount--;
    numEl.textContent = emergencyCount;

    // Animate circle: drain from full → 0
    const progress = emergencyCount / EMERGENCY_COUNTDOWN;
    circleEl.style.strokeDashoffset = CIRCUMFERENCE * (1 - progress);

    if (emergencyCount <= 0) {
      clearInterval(emergencyTimer);
      closeEmergency();
      showToast("info", "⏱ Đã hủy tự động — hết thời gian xác nhận");
    }
  }, 1000);
}

function closeEmergency() {
  clearInterval(emergencyTimer);
  document.getElementById("emergency-modal").classList.remove("show");
}

async function confirmEmergency() {
  clearInterval(emergencyTimer);

  const confirmBtn = document.getElementById("emergency-confirm-btn");
  const cancelBtn  = document.getElementById("emergency-cancel-btn");
  confirmBtn.disabled = true;
  cancelBtn.disabled  = true;
  confirmBtn.innerHTML = `<span class="spinner"></span> Đang gửi...`;

  // Gọi API unlock → Flask → MQTT → ESP32
  const data = await apiFetch("/unlock", { method: "POST" });

  closeEmergency();

  if (data && data.status === "success") {
    const mqttOk = data.mqtt_sent;
    showToast(
      "success",
      mqttOk
        ? "🔓 Đã gửi lệnh MQTT! ESP32 đang mở khóa..."
        : "⚠️ Lệnh đã ghi nhưng MQTT chưa kết nối với ESP32",
      5000
    );
    loadRecentLogs();
    loadStats();
  } else {
    showToast("error", "❌ Không thể gửi lệnh mở khóa");
  }
}

