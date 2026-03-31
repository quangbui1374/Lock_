const API_BASE = "";  // empty = same origin (Flask serves frontend)

// ===== STATE =====
let allLogs = [];
let allUsers = [];
let currentFilter = "all";
let capturedImage = null;
let stream = null;
let currentDeleteTarget = null;
let emergencyTarget = "door1"; // which door the emergency is for
let surgeryTimerInterval = null;   // interval cho định kỳ cập nhật thời gian ca mổ
let surgeryStartTimestamp = 0;     // timestamp bắt đầu ca mổ

// ===== INIT =====
document.addEventListener("DOMContentLoaded", () => {
  initNavigation();
  loadStats();
  loadRecentLogs();
  loadUsers();
  loadLogs();
  loadSurgeryStatus();  // ← load trạng thái ca mổ
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
    const { user, status, time, surgery_ongoing } = data;
    console.log("[SSE] Nhận diện:", data);

    if (status === "granted") {
      if (surgery_ongoing) {
        showToast("info", `🚨 <b>${user}</b> vào Phòng Mổ (ca mổ đang diễn ra)`, 4000);
      } else {
        showToast("success", `🔓 <b>${user}</b> vào Phòng Mổ lúc ${time}`, 6000);
      }
      // Cập nhật trạng thái cửa 2
      updateDoorStatus("door2", "open", surgery_ongoing ? null : user);
      setTimeout(() => updateDoorStatus("door2", "closed"), 30000);
    } else {
      showToast("warning", `⚠️ Nhận diện thất bại lúc ${time}`, 5000);
    }

    loadStats();
    loadRecentLogs();
    loadLogs();

    const activePage = document.querySelector(".page.active");
    if (activePage && activePage.id === "page-dashboard") {
      flashDashboard(status);
    }
  });

  // ── Surgery State Update ──
  sseSource.addEventListener("surgery_update", (e) => {
    const data = JSON.parse(e.data);
    console.log("[SSE] Surgery update:", data);
    updateSurgeryPanel(data);

    if (data.ongoing) {
      showToast("info", `🏥 Ca mổ bắt đầu — BS: <b>${data.surgeon}</b>`, 6000);
    } else if (data.completed_surgeon) {
      const mins = Math.floor((data.elapsed_sec || 0) / 60);
      showToast("success", `✅ Ca mổ hoàn thành — BS: <b>${data.completed_surgeon}</b> (${mins} phút)`, 8000);
    }
    loadRecentLogs();
    loadLogs();
  });

  sseSource.onerror = () => {
    console.warn("[SSE] Mất kết nối, thử lại sau 3s...");
    document.getElementById("server-status-dot").className = "status-indicator offline";
    document.getElementById("server-status-text").textContent = "Mất kết nối";
    sseSource.close();
    setTimeout(connectSSE, 3000);
  };
}

// Cập nhật trạng thái cửa trên dashboard
function updateDoorStatus(door, state, doctorName) {
  if (door === "door1") {
    const badge = document.getElementById("door1-badge");
    const card  = document.getElementById("door1-card");
    if (!badge) return;
    if (state === "open") {
      badge.textContent = "🔓 Đang mở";
      badge.style.background = "rgba(74,222,128,0.15)";
      badge.style.color = "#4ade80";
      card.style.borderColor = "rgba(74,222,128,0.4)";
    } else {
      badge.textContent = "🔒 Đang đóng";
      badge.style.background = "";
      badge.style.color = "";
      card.style.borderColor = "";
    }
  } else {
    const badge  = document.getElementById("door2-badge");
    const card   = document.getElementById("door2-card");
    const docRow = document.getElementById("door2-doctor");
    const docName = document.getElementById("door2-doctor-name");
    if (!badge) return;
    if (state === "open") {
      badge.textContent = "🔓 Đang mở";
      badge.style.background = "rgba(139,92,246,0.2)";
      badge.style.color = "#a78bfa";
      card.style.borderColor = "rgba(139,92,246,0.5)";
      if (doctorName && docRow && docName) {
        docRow.style.display = "block";
        docName.textContent = doctorName;
      }
    } else {
      badge.textContent = "🔒 Đang đóng";
      badge.style.background = "";
      badge.style.color = "";
      card.style.borderColor = "";
      if (docRow) docRow.style.display = "none";
    }
  }
}

// Flash hiệu ứng khi có người quét
function flashDashboard(status) {
  const color = status === "granted"
    ? "rgba(74, 222, 128, 0.08)"
    : "rgba(248, 113, 113, 0.08)";
  const dashboard = document.getElementById("page-dashboard");
  dashboard.style.transition = "background 0.2s";
  dashboard.style.background = color;
  setTimeout(() => { dashboard.style.background = ""; }, 800);
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
  loadSurgeryStatus();
  showToast("success", "🔄 Đã làm mới dữ liệu!");
}

// ===== SURGERY STATE MACHINE =====
async function loadSurgeryStatus() {
  const data = await apiFetch("/surgery/status");
  if (!data) return;
  updateSurgeryPanel(data);
}

function updateSurgeryPanel(data) {
  const idleEl   = document.getElementById("surgery-idle");
  const activeEl = document.getElementById("surgery-active");
  const panelInner = document.getElementById("surgery-panel-inner");
  if (!idleEl || !activeEl) return;

  if (data.ongoing) {
    idleEl.style.display = "none";
    activeEl.style.display = "block";
    panelInner.classList.add("active");

    // Cập nhật tên bác sĩ
    const nameEl = document.getElementById("surgery-surgeon-name");
    const avatarEl = document.getElementById("surgery-avatar");
    if (nameEl) nameEl.textContent = data.surgeon || "—";
    if (avatarEl) avatarEl.textContent = (data.surgeon || "B")[0].toUpperCase();

    // Bắt đầu đếm thời gian
    surgeryStartTimestamp = data.timestamp || (Date.now() / 1000 - (data.elapsed_sec || 0));
    startSurgeryTimer();
  } else {
    idleEl.style.display = "flex";
    activeEl.style.display = "none";
    panelInner.classList.remove("active");
    stopSurgeryTimer();
  }
}

function startSurgeryTimer() {
  stopSurgeryTimer();
  const timerEl = document.getElementById("surgery-timer");
  if (!timerEl) return;

  function tick() {
    const elapsed = Math.floor(Date.now() / 1000 - surgeryStartTimestamp);
    const h = String(Math.floor(elapsed / 3600)).padStart(2, '0');
    const m = String(Math.floor((elapsed % 3600) / 60)).padStart(2, '0');
    const s = String(elapsed % 60).padStart(2, '0');
    timerEl.textContent = `${h}:${m}:${s}`;
  }
  tick();
  surgeryTimerInterval = setInterval(tick, 1000);
}

function stopSurgeryTimer() {
  if (surgeryTimerInterval) {
    clearInterval(surgeryTimerInterval);
    surgeryTimerInterval = null;
  }
  const timerEl = document.getElementById("surgery-timer");
  if (timerEl) timerEl.textContent = "00:00:00";
}

async function completeSurgery() {
  if (!confirm("✅ Xác nhận HOÀN THÀNH ca mổ?\n\nHệ thống sẽ:\n• Xóa tên bác sĩ trên màn hình\n• Trả phòng về trạng thái Trống")) {
    return;
  }

  const btn = document.getElementById("btn-complete-surgery");
  if (btn) {
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner"></span> Đang xử lý...';
  }

  const data = await apiFetch("/surgery/complete", { method: "POST" });

  if (btn) {
    btn.disabled = false;
    btn.innerHTML = `
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="20" height="20">
        <polyline points="20 6 9 17 4 12"/>
      </svg>
      Hoàn thành ca mổ`;
  }

  if (data && data.status === "success") {
    showToast("success", `✅ ${data.message}`, 5000);
    updateSurgeryPanel({ ongoing: false });
    loadRecentLogs();
    loadStats();
  } else {
    const msg = data?.message || "Đã xảy ra lỗi";
    showToast("error", `❌ ${msg}`);
  }
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

// ===== RECENT LOGS (Dashboard) - chỉ hiện bác sĩ phòng mổ =====
async function loadRecentLogs() {
  const data = await apiFetch("/logs");
  if (!data) return;

  const container = document.getElementById("recent-logs");
  // Chỉ lấy log access phòng mổ (type = access, granted)
  const recent = data
    .filter(l => l.type === "access" && l.status === "granted")
    .slice(0, 6);

  if (recent.length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">🏥</div>
        <p>Chưa có bác sĩ nào vào phòng mổ</p>
      </div>`;
    return;
  }

  container.innerHTML = recent.map(log => createRecentLogHTML(log)).join("");
}

function createRecentLogHTML(log) {
  const initial = (log.user || "?")[0].toUpperCase();
  return `
    <div class="recent-log-item">
      <div class="log-avatar" style="background:linear-gradient(135deg,#7c3aed,#4f46e5)">${initial}</div>
      <div class="log-info">
        <div class="log-name">👨‍⚕️ ${escapeHtml(log.user || "Unknown")}</div>
        <div class="log-time">${formatTime(log.time)}</div>
      </div>
      <span class="badge badge-granted">✓ Vào PM</span>
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

  // Chỉ hiện log access phòng mổ
  const filtered = logs.filter(l => l.type === "access");

  if (filtered.length === 0) {
    tbody.innerHTML = `
      <tr>
        <td colspan="4">
          <div class="empty-state">
            <div class="empty-icon">🏥</div>
            <p>Chưa có dữ liệu vào phòng mổ</p>
          </div>
        </td>
      </tr>`;
    return;
  }

  tbody.innerHTML = filtered.map((log, i) => {
    const badgeClass = log.status === "granted" ? "badge-granted" : "badge-denied";
    const badgeText  = log.status === "granted" ? "✓ Vào" : "✗ Từ chối";
    const initial = (log.user || "?")[0].toUpperCase();

    return `
      <tr class="row-enter">
        <td class="log-num">${i + 1}</td>
        <td>
          <div class="log-user-cell">
            <div class="icon" style="background:linear-gradient(135deg,#7c3aed,#4f46e5)">${initial}</div>
            <span>👨‍⚕️ ${escapeHtml(log.user || "Unknown")}</span>
          </div>
        </td>
        <td><span class="badge ${badgeClass}">${badgeText}</span></td>
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

// ===== MANUAL UNLOCK (legacy, không dùng nữa) =====
function manualUnlock() { openEmergency('door1'); }
function closeModal() {}
async function confirmUnlock() {}


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
document.getElementById("delete-modal").addEventListener("click", function(e) {
  if (e.target === this) closeDeleteModal();
});
document.getElementById("emergency-modal").addEventListener("click", function(e) {
  if (e.target === this) closeEmergency();
});

// Make sure modals are hidden on init
document.getElementById("delete-modal").classList.remove("show");

// ===== EMERGENCY UNLOCK =====
const EMERGENCY_COUNTDOWN = 5;
const CIRCUMFERENCE = 2 * Math.PI * 33;
let emergencyTimer  = null;
let emergencyCount  = EMERGENCY_COUNTDOWN;

function openEmergency(door) {
  emergencyTarget = door || "door1";
  emergencyCount = EMERGENCY_COUNTDOWN;

  // Cập nhật tiêu đề modal theo cửa
  const title = document.getElementById("emergency-modal-title");
  const desc  = document.getElementById("emergency-modal-desc");
  if (emergencyTarget === "door2") {
    if (title) title.textContent = "⚠️ Mở khẩn cấp Phòng Mổ";
    if (desc) desc.textContent = "Mở cửa Phòng Mổ ngay lập tức qua MQTT";
  } else {
    if (title) title.textContent = "⚠️ Mở khẩn cấp Cửa ngoài";
    if (desc)  desc.textContent = "Mở cửa ngoài ngay lập tức qua MQTT";
  }

  const numEl     = document.getElementById("countdown-number");
  const circleEl  = document.getElementById("countdown-circle");
  const confirmBtn = document.getElementById("emergency-confirm-btn");
  const cancelBtn  = document.getElementById("emergency-cancel-btn");

  numEl.textContent = emergencyCount;
  circleEl.style.strokeDashoffset = 0;
  confirmBtn.disabled = false;
  cancelBtn.disabled  = false;

  document.getElementById("emergency-modal").classList.add("show");

  clearInterval(emergencyTimer);
  emergencyTimer = setInterval(() => {
    emergencyCount--;
    numEl.textContent = emergencyCount;
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

  // Gửi đến đúng route tùy theo cửa
  const url = emergencyTarget === "door2" ? "/unlock_door2" : "/unlock";
  const data = await apiFetch(url, { method: "POST", body: JSON.stringify({user: "Khẩn cấp (Web)"}) });

  closeEmergency();

  if (data && data.status === "success") {
    const mqttOk = data.mqtt_sent;
    const doorLabel = emergencyTarget === "door2" ? "Phòng Mổ" : "Cửa ngoài";
    showToast(
      "success",
      mqttOk
        ? `🔓 Đã gửi lệnh MQTT! ${doorLabel} đang mở...`
        : `⚠️ Ghi nhận nhưng MQTT chưa kết nối ESP32`,
      5000
    );
    if (emergencyTarget === "door1") updateDoorStatus("door1", "open");
    if (emergencyTarget === "door2") updateDoorStatus("door2", "open", "Khẩn cấp");
    loadRecentLogs();
    loadStats();
  } else {
    showToast("error", "❌ Không thể gửi lệnh mở khóa");
  }
}
