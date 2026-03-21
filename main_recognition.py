import cv2
import json
import numpy as np
import time
import requests
import base64
import os
from deepface import DeepFace
from ultralytics import YOLO

# ================== CONFIG ==================
DB_FILE = "db.json"
MODEL_PATH = r"C:\Users\quang\PycharmProjects\PythonProject1\yolov8l-face.pt"
model_name = "Facenet"

# ──────────────────────────────────────────────────────
# THRESHOLD — quan trọng nhất!
# Facenet Cosine Distance:
#   0.0 = giống hệt nhau
#   1.0 = hoàn toàn khác nhau
#
#   < 0.30  rất nghiêm (ít nhầm nhất, có thể bỏ sót)
#   < 0.38  khuyên dùng ✔
#   < 0.50  tạm chấp nhận
#   < 0.65  QUÁ CAO → nhận nhầm người!  ✗
# ──────────────────────────────────────────────────────
threshold = 0.38  # ← giảm từ 0.65 xuống 0.38

FRAME_SKIP = 5
RESIZE_WIDTH = 640

# ──────────────────────────────────────────────────────
# XÁC NHẬN ĐA FRAME — chống nhận nhầm
# Phải nhận dạng đúng CONFIRM_FRAMES liên tiếp
# mới được chấp nhận là granted
# ──────────────────────────────────────────────────────
CONFIRM_FRAMES = 3  # cần 3 frame liên tiếp cùng kết quả

# ── Địa chỉ Flask Web Server ──
WEB_SERVER = "http://127.0.0.1:5000"
COOLDOWN = 5  # giây giữa 2 lần gửi cùng 1 người

# ================== LOAD DB (Real-time update) ==================
database = {}
last_db_mtime = 0

# Khởi tạo YOLO trước để dùng quét ảnh lúc load DB
face_detector = YOLO(MODEL_PATH)


def load_database():
    global database, last_db_mtime
    if not os.path.exists(DB_FILE):
        return

    try:
        current_mtime = os.path.getmtime(DB_FILE)
        if current_mtime <= last_db_mtime:
            return  # DB chưa thay đổi

        print(f"[DB] Đang cập nhật cơ sở dữ liệu...")
        with open(DB_FILE, "r") as f:
            raw_db = json.load(f)

        new_database = {}
        for name, info in raw_db.items():
            if isinstance(info, list):
                # Format cũ (lưu sẵn embedding)
                new_database[name] = info if isinstance(info[0], list) else [info]
            elif isinstance(info, dict) and "full_image" in info:
                # Format mới (base64 từ web) - Cần crop mặt giống hệt code nhận diện
                if name in database:
                    new_database[name] = database[name]
                else:
                    try:
                        b64_str = info["full_image"].split(",")[-1] if "," in info["full_image"] else info["full_image"]
                        img_data = base64.b64decode(b64_str)
                        nparr = np.frombuffer(img_data, np.uint8)
                        img_cv2 = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                        if img_cv2 is not None:
                            results = face_detector(img_cv2, verbose=False)
                            face_found = False

                            for r in results:
                                if r.boxes is not None and len(r.boxes) > 0:
                                    box = r.boxes[0]
                                    x1, y1, x2, y2 = map(int, box.xyxy[0])
                                    h, w = img_cv2.shape[:2]
                                    x1, y1 = max(0, x1), max(0, y1)
                                    x2, y2 = min(w, x2), min(h, y2)

                                    if x2 <= x1 or y2 <= y1:
                                        continue

                                    face_img = img_cv2[y1:y2, x1:x2]
                                    if face_img.size > 0:
                                        face_rgb = cv2.cvtColor(face_img, cv2.COLOR_BGR2RGB)
                                        emb = DeepFace.represent(
                                            img_path=face_rgb,
                                            model_name=model_name,
                                            enforce_detection=False
                                        )[0]["embedding"]
                                        new_database[name] = [emb]
                                        print(f"[DB] ✔ Đã nhận dạng & học khuôn mặt: {name}")
                                        face_found = True
                                        break

                            if not face_found:
                                print(f"[DB] ✗ Không tìm thấy khuôn mặt rõ ràng trong ảnh của: {name}")
                        else:
                            print(f"[DB] ✗ Ảnh trống: {name}")
                    except Exception as e:
                        print(f"[DB] ✗ Lỗi parse {name}: {e}")

        database = new_database
        last_db_mtime = current_mtime
        print(f"[DB] Đã tải {len(database)} người dùng!")
    except Exception as e:
        print("[DB] Lỗi reload DB:", e)


# Gọi 1 lần lúc startup
load_database()


# ================== UTILS ==================
def cosine_distance(a, b):
    a = np.array(a).flatten()
    b = np.array(b).flatten()
    return float(1 - np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))


# ================== GỬI KẾT QUẢ LÊN WEB ==================
def send_result(user: str, status: str):
    try:
        res = requests.post(
            f"{WEB_SERVER}/recognize",
            json={"user": user, "status": status},
            timeout=2
        )
        if res.status_code == 200:
            print(f"[WEB] Sent: {user} = {status}")
    except requests.exceptions.ConnectionError:
        print("[WEB] Không kết nối được Flask")
    except Exception as e:
        print(f"[WEB] Lỗi: {e}")


# ================== NHẬN DIỆN ==================
def identify_face(frame):
    """Trả về (user, box, confidence%)"""
    results = face_detector(frame, verbose=False)

    identified_user = None
    best_distance = float('inf')
    best_box = None

    for r in results:
        boxes = r.boxes
        if boxes is None or len(boxes) == 0:
            return None, None, 0.0

        boxes = sorted(boxes, key=lambda b: float(b.conf[0]), reverse=True)
        box = boxes[0]
        if float(box.conf[0]) < 0.5:
            return None, None, 0.0

        x1, y1, x2, y2 = map(int, box.xyxy[0])
        h, w = frame.shape[:2]
        x1, y1 = max(0, x1), max(0, y1)
        x2, y2 = min(w, x2), min(h, y2)

        face_img = frame[y1:y2, x1:x2]
        if face_img.size == 0:
            return None, None, 0.0

        try:
            face_rgb = cv2.cvtColor(face_img, cv2.COLOR_BGR2RGB)
            current_embedding = DeepFace.represent(
                img_path=face_rgb,
                model_name=model_name,
                enforce_detection=False
            )[0]["embedding"]
        except Exception:
            return None, None, 0.0

        for user_name, embeddings in database.items():
            for emb in embeddings:
                dist = cosine_distance(current_embedding, emb)
                # In debug ra Terminal để thấy khoảng cách thật
                print(f"[DEBUG] Khách vs {user_name} -> distance = {dist:.3f} (threshold={threshold})")

                if dist < threshold and dist < best_distance:
                    best_distance = dist
                    identified_user = user_name
                    best_box = (x1, y1, x2, y2)

        # Phát hiện mặt nhưng không khớp DB → Unknown
        if identified_user is None:
            best_box = (x1, y1, x2, y2)

    if identified_user:
        confidence = round((1 - best_distance) * 100, 1)
        return identified_user, best_box, confidence

    return "Unknown", best_box, 0.0


# ================== MAIN LOOP ==================
def run_system():
    global threshold  # cho phép chỉnh threshold real-time

    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, RESIZE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    frame_count = 0
    result = None
    confidence = 0.0
    box = None

    last_sent = {}  # {user: timestamp}
    confirm_counter = 0  # đếm frame liên tiếp cùng kết quả
    pending_user = None  # người đang chờ xác nhận

    print("=" * 55)
    print("  FaceGuard AI — đang chạy...")
    print(f"  Threshold   : {threshold}  (cosine distance)")
    print(f"  Xác nhận    : {CONFIRM_FRAMES} frame liên tiếp")
    print(f"  Web server  : {WEB_SERVER}")
    print("  Phím: Q=thoát | +=tăng threshold | -=giảm threshold")
    print("=" * 55)

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        frame = cv2.resize(frame, (RESIZE_WIDTH, 480))
        frame_count += 1

        # ── Nhận diện mỗi FRAME_SKIP frame ──
        if frame_count % FRAME_SKIP == 0:
            # Liên tục kiểm tra file db.json (nếu ai đó mới đăng ký trên Web)
            load_database()

            result, box, confidence = identify_face(frame)

            # ── Xác nhận đa frame ──
            if result and result != "Unknown":
                if result == pending_user:
                    confirm_counter += 1
                else:
                    pending_user = result
                    confirm_counter = 1
            else:
                pending_user = None
                confirm_counter = 0

            # ── Chỉ gửi khi đủ frame xác nhận ──
            if confirm_counter >= CONFIRM_FRAMES:
                now = time.time()
                last_t = last_sent.get(result, 0)

                if now - last_t > COOLDOWN:
                    send_result(result, "granted")
                    last_sent[result] = now
                    print(f"[OK] {result} | Similarity: {confidence:.1f}% | dist={1 - confidence / 100:.3f}")

                confirm_counter = 0  # reset sau khi gửi

            elif result == "Unknown" and box is not None:
                now = time.time()
                last_t = last_sent.get("Unknown", 0)
                if now - last_t > COOLDOWN:
                    send_result("Unknown", "denied")
                    last_sent["Unknown"] = now
                    print("[!!] Người lạ phát hiện!")

        # ── Vẽ bounding box ──
        if box:
            x1, y1, x2, y2 = box
            is_known = result and result != "Unknown"
            color = (0, 255, 0) if is_known else (0, 0, 255)

            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

            if result:
                label = f"{result} ({confidence:.0f}%)" if is_known else "Unknown"
                cv2.putText(frame, label, (x1, y1 - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.75, color, 2)

            # Thanh xác nhận đa frame
            if pending_user and is_known:
                bar_w = x2 - x1
                filled = int(bar_w * min(confirm_counter / CONFIRM_FRAMES, 1.0))
                cv2.rectangle(frame, (x1, y2 + 4), (x2, y2 + 14), (40, 40, 40), -1)
                cv2.rectangle(frame, (x1, y2 + 4), (x1 + filled, y2 + 14), (0, 200, 100), -1)

        # ── HUD ──
        cv2.putText(frame,
                    f"Threshold: {threshold:.2f} | Confirm: {confirm_counter}/{CONFIRM_FRAMES}",
                    (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (160, 160, 160), 1)

        cv2.imshow("FaceGuard AI Recognition", frame)
        key = cv2.waitKey(1) & 0xFF

        if key == ord('q'):
            break
        elif key == ord('+'):
            threshold = round(min(threshold + 0.02, 0.70), 2)
            print(f"[CONFIG] threshold → {threshold}")
        elif key == ord('-'):
            threshold = round(max(threshold - 0.02, 0.15), 2)
            print(f"[CONFIG] threshold → {threshold}")

    cap.release()
    cv2.destroyAllWindows()


# ================== RUN ==================
if __name__ == "__main__":
    run_system()
