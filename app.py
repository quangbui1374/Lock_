from flask import Flask, request, jsonify, send_from_directory, Response
import json
import time
import os
import threading
import queue
import paho.mqtt.client as mqtt_client

app = Flask(__name__, static_folder='static')

DB_FILE  = "db.json"
LOG_FILE = "log.json"

# ──────────────────────────────────────────
#  CẤU HÌNH MQTT
# ──────────────────────────────────────────
MQTT_BROKER  = "broker.hivemq.com"
MQTT_PORT    = 1883
MQTT_PREFIX  = "smartlock/quang_1307"

TOPIC_COMMAND   = f"{MQTT_PREFIX}/command"    # gửi lệnh -> ESP
TOPIC_STATUS    = f"{MQTT_PREFIX}/status"     # nhận trạng thái <- ESP
TOPIC_HEARTBEAT = f"{MQTT_PREFIX}/heartbeat"  # nhận heartbeat <- ESP

esp_online   = False   # trạng thái ESP
mqtt_client_ = None    # instance mqtt

# ──────────────────────────────────────────
#  MQTT CALLBACKS  (paho-mqtt v2.x)
# ──────────────────────────────────────────
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("[MQTT] Connected to HiveMQ broker")
        client.subscribe(TOPIC_STATUS)
        client.subscribe(TOPIC_HEARTBEAT)
        print(f"[MQTT] Subscribed: {TOPIC_STATUS}")
        print(f"[MQTT] Subscribed: {TOPIC_HEARTBEAT}")
    else:
        print(f"[MQTT] Connect failed, code={reason_code}")

def on_message(client, userdata, msg):
    global esp_online
    try:
        payload = json.loads(msg.payload.decode())
        topic   = msg.topic
        print(f"[MQTT] Received [{topic}]: {payload}")

        if topic == TOPIC_HEARTBEAT:
            esp_online = True

        elif topic == TOPIC_STATUS:
            event   = payload.get("event", "unknown")
            trigger = payload.get("trigger", "esp")
            esp_online = True

            if event in ("unlocked", "locked"):
                log_data = {
                    "user": f"ESP ({trigger})",
                    "status": "granted" if event == "unlocked" else "locked",
                    "type":   "esp_event",
                    "event":  event,
                    "time":   time.strftime("%Y-%m-%d %H:%M:%S"),
                    "timestamp": time.time()
                }
                save_log(log_data)

    except Exception as e:
        print(f"[MQTT] Message error: {e}")

def on_disconnect(client, userdata, flags, reason_code, properties):
    global esp_online
    esp_online = False
    print(f"[MQTT] Disconnected, code={reason_code}")

# ──────────────────────────────────────────
#  KHỞI ĐỘNG MQTT TRONG THREAD RIÊNG
# ──────────────────────────────────────────
def start_mqtt():
    global mqtt_client_
    client = mqtt_client.Client(
        mqtt_client.CallbackAPIVersion.VERSION2,
        client_id="flask-smartlock-quang1307"
    )
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client_ = client
        client.loop_forever()
    except Exception as e:
        print(f"[MQTT] Cannot connect: {e}")


# Khởi động thread MQTT khi import module
_mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
_mqtt_thread.start()

# ──────────────────────────────────────────
#  HELPER: Publish command đến ESP
# ──────────────────────────────────────────
def mqtt_publish_command(action: str, user: str = "Unknown"):
    if mqtt_client_ and mqtt_client_.is_connected():
        time_str = time.strftime("%H:%M")
        payload = json.dumps({"action": action, "user": user, "time": time_str})
        mqtt_client_.publish(TOPIC_COMMAND, payload)
        print(f"[MQTT] Published command: {payload}")
        return True
    else:
        print("[MQTT] Not connected – command not sent")
        return False

# ──────────────────────────────────────────
#  SSE — SERVER-SENT EVENTS
#  Browser mở 1 kết nối dài tới /events
#  Flask push JSON xuống ngay khi có sự kiện
# ──────────────────────────────────────────
_sse_lock        = threading.Lock()
_sse_subscribers = []   # list of queue.Queue

def sse_push(event_type: str, data: dict):
    """Gửi event tới tất cả browser đang kết nối."""
    msg = f"event: {event_type}\ndata: {json.dumps(data, ensure_ascii=False)}\n\n"
    with _sse_lock:
        dead = []
        for q in _sse_subscribers:
            try:
                q.put_nowait(msg)
            except queue.Full:
                dead.append(q)
        for q in dead:
            _sse_subscribers.remove(q)

# ===== HELPERS =====
def load_db():
    try:
        with open(DB_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except:
        return {}

def save_db(db):
    with open(DB_FILE, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=4, ensure_ascii=False)

def load_logs():
    try:
        with open(LOG_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except:
        return []

def save_log(data):
    logs = load_logs()
    logs.insert(0, data)  # newest first
    with open(LOG_FILE, "w", encoding="utf-8") as f:
        json.dump(logs, f, indent=4, ensure_ascii=False)

# ===== SERVE FRONTEND =====
@app.route("/")
def index():
    return send_from_directory("static", "index.html")

# ===== REGISTER =====
@app.route("/register", methods=["POST"])
def register():
    try:
        data = request.json
        name = data.get("name", "").strip()
        img_base64 = data.get("image", "")

        if not name:
            return jsonify({"status": "error", "message": "Tên không được để trống"}), 400

        db = load_db()

        if name not in db:
            db[name] = {
                "registered_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "access_count": 0,
                "avatar": img_base64[:100] + "..." if img_base64 else "",
                "full_image": img_base64
            }
        else:
            # Update image if re-registering
            db[name]["full_image"] = img_base64
            db[name]["avatar"] = img_base64[:100] + "..." if img_base64 else ""

        save_db(db)

        log_data = {
            "user": name,
            "status": "registered",
            "type": "register",
            "time": time.strftime("%Y-%m-%d %H:%M:%S"),
            "timestamp": time.time()
        }
        save_log(log_data)

        return jsonify({"status": "success", "message": f"Đã đăng ký {name} thành công!"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# ===== LOG FROM AI SERVER =====
@app.route("/log", methods=["POST"])
def log():
    try:
        data = request.json
        user = data.get("user", "Unknown")
        status = data.get("status", "unknown")

        log_data = {
            "user": user,
            "status": status,
            "type": "access",
            "time": time.strftime("%Y-%m-%d %H:%M:%S"),
            "timestamp": time.time()
        }

        save_log(log_data)

        # Update access count if granted
        if status == "granted":
            db = load_db()
            if user in db:
                db[user]["access_count"] = db[user].get("access_count", 0) + 1
                db[user]["last_access"] = time.strftime("%Y-%m-%d %H:%M:%S")
                save_db(db)

        return jsonify({"status": "logged"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# ===== RECOGNIZE (simulate from AI) =====
@app.route("/recognize", methods=["POST"])
def recognize():
    try:
        data = request.json
        user = data.get("user", "Unknown")
        status = data.get("status", "denied")

        log_data = {
            "user": user,
            "status": status,
            "type": "access",
            "time": time.strftime("%Y-%m-%d %H:%M:%S"),
            "timestamp": time.time()
        }
        save_log(log_data)

        if status == "granted":
            db = load_db()
            if user in db:
                db[user]["access_count"] = db[user].get("access_count", 0) + 1
                db[user]["last_access"] = time.strftime("%Y-%m-%d %H:%M:%S")
                save_db(db)

            # Gửi MQTT mở khóa kèm user + time
            mqtt_publish_command("unlock", user)

        # ✔ Push SSE xuống tất cả browser ngay lập tức
        sse_push("recognize", {
            "user":   user,
            "status": status,
            "time":   time.strftime("%Y-%m-%d %H:%M:%S")
        })

        return jsonify({"status": "ok", "unlock": status == "granted"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# ===== GET USERS =====
@app.route("/users", methods=["GET"])
def users():
    db = load_db()
    result = []
    for name, info in db.items():
        result.append({
            "name": name,
            "registered_at": info.get("registered_at", "N/A"),
            "access_count": info.get("access_count", 0),
            "last_access": info.get("last_access", "Chưa truy cập"),
            "avatar": info.get("full_image", "")
        })
    return jsonify(result)

# ===== DELETE USER =====
@app.route("/users/<name>", methods=["DELETE"])
def delete_user(name):
    try:
        db = load_db()
        if name in db:
            del db[name]
            save_db(db)
            return jsonify({"status": "success", "message": f"Đã xóa {name}"})
        else:
            return jsonify({"status": "error", "message": "Không tìm thấy user"}), 404
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# ===== GET LOGS =====
@app.route("/logs", methods=["GET"])
def logs():
    logs = load_logs()
    return jsonify(logs)

# ===== CLEAR LOGS =====
@app.route("/logs/clear", methods=["DELETE"])
def clear_logs():
    with open(LOG_FILE, "w") as f:
        json.dump([], f)
    return jsonify({"status": "success"})

# ===== STATS =====
@app.route("/stats", methods=["GET"])
def stats():
    db = load_db()
    logs = load_logs()

    total_users = len(db)
    total_access = len([l for l in logs if l.get("type") == "access"])
    granted = len([l for l in logs if l.get("status") == "granted"])
    denied = len([l for l in logs if l.get("status") == "denied"])

    return jsonify({
        "total_users": total_users,
        "total_access": total_access,
        "granted": granted,
        "denied": denied
    })

# ===== UNLOCK (manual from web) → gửi MQTT đến ESP =====
@app.route("/unlock", methods=["POST"])
def unlock():
    # Gửi lệnh unlock qua MQTT -> ESP32
    sent = mqtt_publish_command("unlock", "Thủ công")

    log_data = {
        "user": "Manual (Web)",
        "status": "granted",
        "type": "manual_unlock",
        "time": time.strftime("%Y-%m-%d %H:%M:%S"),
        "timestamp": time.time()
    }
    save_log(log_data)

    msg = "Đã gửi lệnh MQTT đến ESP32" if sent else "MQTT chưa kết nối – lệnh chưa gửi được"
    return jsonify({"status": "success", "message": msg, "mqtt_sent": sent})

# ===== SSE ENDPOINT =====
@app.route("/events")
def events():
    """Browser kết nối ở đây để nhận push real-time."""
    def stream():
        q = queue.Queue(maxsize=20)
        with _sse_lock:
            _sse_subscribers.append(q)
        try:
            # Gửi heartbeat ngay khi kết nối
            yield "event: connected\ndata: {}\n\n"
            while True:
                try:
                    msg = q.get(timeout=25)   # 25s timeout
                    yield msg
                except queue.Empty:
                    yield ": heartbeat\n\n"  # giữ kết nối khỏi bị ngắt
        except GeneratorExit:
            pass
        finally:
            with _sse_lock:
                if q in _sse_subscribers:
                    _sse_subscribers.remove(q)

    return Response(
        stream(),
        mimetype="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no"
        }
    )

# ===== ESP STATUS (online/offline check) =====
@app.route("/esp/status", methods=["GET"])
def esp_status():
    return jsonify({
        "online": esp_online,
        "mqtt_broker": MQTT_BROKER,
        "topic_command": TOPIC_COMMAND
    })

if __name__ == "__main__":
    # threaded=True bắt buộc để SSE hoạt động đúng
    app.run(debug=True, host="0.0.0.0", port=5000, threaded=True)
