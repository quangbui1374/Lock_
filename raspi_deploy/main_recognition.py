import cv2
import json
import numpy as np
import time
import requests
import base64
import os

# ================== CONFIG ==================
DB_FILE = "/home/quang/smartlock/db.json"
WEB_SERVER = "http://127.0.0.1:5000"

# Bo phat hien mat Haarcascade (co san trong OpenCV)
face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
)

# Bo nhan dien mat LBPH (co san trong OpenCV, khong can TensorFlow)
recognizer = cv2.face.LBPHFaceRecognizer_create()

# ================== SETTINGS ==================
CONFIDENCE_THRESHOLD = 80   # Duoi nguong nay moi chap nhan (LBPH: so cang nho cang giong)
FRAME_SKIP = 3              # Xu ly moi N frame (giam tai cho Pi)
RESIZE_WIDTH = 480
CONFIRM_FRAMES = 3          # Can N frame lien tiep cung ket qua
COOLDOWN = 5                # Giay giua 2 lan gui cung 1 nguoi
FACE_SIZE = (200, 200)      # Kich thuoc chuan hoa khuon mat

# ================== DATABASE ==================
label_map = {}        # {0: "Dr. Quang", 1: "Dr. An", ...}
last_db_mtime = 0
is_trained = False

def load_database():
    """Doc db.json, crop mat, train LBPH recognizer."""
    global label_map, last_db_mtime, is_trained

    if not os.path.exists(DB_FILE):
        print("[DB] File db.json khong ton tai!")
        return

    try:
        current_mtime = os.path.getmtime(DB_FILE)
        if current_mtime <= last_db_mtime:
            return  # DB chua thay doi
        
        print("[DB] Dang cap nhat co so du lieu...")
        with open(DB_FILE, "r") as f:
            raw_db = json.load(f)

        faces = []
        labels = []
        new_label_map = {}
        label_id = 0

        for name, info in raw_db.items():
            if not isinstance(info, dict) or "full_image" not in info:
                continue
            if not info["full_image"]:
                continue

            try:
                # Giai ma anh base64
                b64_str = info["full_image"]
                if "," in b64_str:
                    b64_str = b64_str.split(",")[-1]
                img_data = base64.b64decode(b64_str)
                nparr = np.frombuffer(img_data, np.uint8)
                img_cv2 = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                if img_cv2 is None:
                    print(f"[DB] Anh trong: {name}")
                    continue

                # Chuyen sang grayscale
                gray = cv2.cvtColor(img_cv2, cv2.COLOR_BGR2GRAY)

                # Phat hien mat trong anh dang ky
                detected = face_cascade.detectMultiScale(
                    gray, scaleFactor=1.1, minNeighbors=5, minSize=(60, 60)
                )

                if len(detected) == 0:
                    print(f"[DB] Khong tim thay mat: {name}")
                    continue

                # Lay mat lon nhat
                (x, y, w, h) = max(detected, key=lambda r: r[2] * r[3])
                face_roi = gray[y:y+h, x:x+w]
                face_resized = cv2.resize(face_roi, FACE_SIZE)

                # Them vao tap huan luyen
                # Tao nhieu bien the de tang do chinh xac
                faces.append(face_resized)
                labels.append(label_id)

                # Tang cuong du lieu (Data Augmentation)
                # Lat ngang
                faces.append(cv2.flip(face_resized, 1))
                labels.append(label_id)

                # Tang do sang
                bright = cv2.convertScaleAbs(face_resized, alpha=1.2, beta=20)
                faces.append(bright)
                labels.append(label_id)

                # Giam do sang
                dark = cv2.convertScaleAbs(face_resized, alpha=0.8, beta=-20)
                faces.append(dark)
                labels.append(label_id)

                # Lam mo nhe (mo phong camera chat luong thap)
                blurred = cv2.GaussianBlur(face_resized, (3, 3), 0)
                faces.append(blurred)
                labels.append(label_id)

                new_label_map[label_id] = name
                label_id += 1
                print(f"[DB] Da hoc mat: {name} (5 mau)")

            except Exception as e:
                print(f"[DB] Loi xu ly {name}: {e}")

        if len(faces) > 0:
            recognizer.train(faces, np.array(labels))
            label_map = new_label_map
            is_trained = True
            last_db_mtime = current_mtime
            print(f"[DB] Hoan tat! Da hoc {len(label_map)} nguoi dung ({len(faces)} mau)")
        else:
            print("[DB] Khong co khuon mat nao de hoc!")
            is_trained = False

    except Exception as e:
        print(f"[DB] Loi: {e}")


def identify_face(frame):
    """Nhan dien khuon mat trong frame. Tra ve (ten, box, confidence)."""
    if not is_trained:
        return None, None, 0.0

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    faces = face_cascade.detectMultiScale(
        gray, scaleFactor=1.1, minNeighbors=5, minSize=(60, 60)
    )

    if len(faces) == 0:
        return None, None, 0.0

    # Lay mat lon nhat (gan camera nhat)
    (x, y, w, h) = max(faces, key=lambda r: r[2] * r[3])
    face_roi = gray[y:y+h, x:x+w]
    face_resized = cv2.resize(face_roi, FACE_SIZE)

    try:
        label, confidence = recognizer.predict(face_resized)
        # LBPH: confidence cang THAP cang GIONG (nguoc voi cosine distance)
        print(f"[AI] Label={label}, Confidence={confidence:.1f} (threshold={CONFIDENCE_THRESHOLD})")

        if confidence < CONFIDENCE_THRESHOLD and label in label_map:
            name = label_map[label]
            # LBPH distance (0-100), bien thanh % tu 60% - 99%
            similarity = min(99.9, max(0.0, 100 - (confidence / 2)))
            return name, (x, y, x + w, y + h), round(similarity, 1)
    except Exception as e:
        print(f"[AI] Loi predict: {e}")

    return "Unknown", (x, y, x + w, y + h), 0.0


def send_result(user, status):
    """Gui ket qua nhan dien len Flask Web Server."""
    try:
        requests.post(
            f"{WEB_SERVER}/recognize",
            json={"user": user, "status": status},
            timeout=2
        )
        print(f"[WEB] Sent: {user} = {status}")
    except requests.exceptions.ConnectionError:
        print("[WEB] Khong ket noi duoc Flask")
    except Exception as e:
        print(f"[WEB] Loi: {e}")


def run_system():
    """Vong lap chinh cua he thong nhan dien."""
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, RESIZE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 360)

    if not cap.isOpened():
        print("[ERROR] Khong mo duoc camera! Kiem tra USB Webcam.")
        print("[TIP] Thu doi VideoCapture(0) thanh VideoCapture(1)")
        return

    # Load database lan dau
    load_database()

    print("=" * 50)
    print("  Smart Lock AI - OpenCV LBPH mode")
    print(f"  Nguong tin cay : {CONFIDENCE_THRESHOLD}")
    print(f"  Xac nhan       : {CONFIRM_FRAMES} frame lien tiep")
    print(f"  Web server     : {WEB_SERVER}")
    print("  Phim: Q = thoat")
    print("=" * 50)

    frame_count = 0
    last_sent = {}
    pending_user = None
    confirm_counter = 0
    result = None
    box = None
    confidence = 0.0

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[CAM] Khong doc duoc frame!")
            break

        frame = cv2.resize(frame, (RESIZE_WIDTH, 360))
        frame_count += 1

        if frame_count % FRAME_SKIP == 0:
            # Kiem tra db.json co thay doi khong
            load_database()

            # Kiem tra web server co dang yeu cau quet mat khong
            waiting_layer2 = False
            try:
                res = requests.get(f"{WEB_SERVER}/status", timeout=2.0)
                if res.status_code == 200:
                    waiting_layer2 = res.json().get("waiting_for_face", False)
                else:
                    print(f"[API] Loi Server tra ve ma: {res.status_code}")
            except Exception as e:
                print(f"[API] Loi ket noi toi Web Server: {e}")

            if not waiting_layer2:
                result, box, confidence = None, None, 0.0
                pending_user = None
                confirm_counter = 0
                if frame_count % (FRAME_SKIP * 10) == 0:
                    print("[INFO] Đang chờ yêu cầu từ STM32 (Layer 1/Password)...")
                cv2.putText(frame, "Waiting Layer 1 (Password)...",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                            (0, 255, 255), 2)
            else:
                cv2.putText(frame, "Layer 2 ACTIVE - Scanning Face!",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                            (0, 255, 0), 2)
                result, box, confidence = identify_face(frame)

                # Xac nhan da frame
                if result and result != "Unknown":
                    if result == pending_user:
                        confirm_counter += 1
                    else:
                        pending_user = result
                        confirm_counter = 1
                else:
                    pending_user = None
                    confirm_counter = 0

                # Gui ket qua khi du frame xac nhan
                if confirm_counter >= CONFIRM_FRAMES:
                    now = time.time()
                    last_t = last_sent.get(result, 0)
                    if now - last_t > COOLDOWN:
                        send_result(result, "granted")
                        last_sent[result] = now
                        print(f"[OK] {result} vao cua! (similarity={confidence}%)")
                    confirm_counter = 0

                elif result == "Unknown" and box is not None:
                    now = time.time()
                    last_t = last_sent.get("Unknown", 0)
                    if now - last_t > COOLDOWN:
                        send_result("Unknown", "denied")
                        last_sent["Unknown"] = now
                        print("[!!] Nguoi la phat hien!")

        # Ve bounding box
        if box:
            x1, y1, x2, y2 = box
            is_known = result and result != "Unknown"
            color = (0, 255, 0) if is_known else (0, 0, 255)
            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

            if result:
                label = f"{result} ({confidence:.0f}%)" if is_known else "Unknown"
                cv2.putText(frame, label, (x1, y1 - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.65, color, 2)

            # Thanh xac nhan da frame
            if pending_user and is_known:
                bar_w = x2 - x1
                filled = int(bar_w * min(confirm_counter / CONFIRM_FRAMES, 1.0))
                cv2.rectangle(frame, (x1, y2 + 4), (x2, y2 + 14), (40, 40, 40), -1)
                cv2.rectangle(frame, (x1, y2 + 4), (x1 + filled, y2 + 14), (0, 200, 100), -1)

        cv2.imshow("Smart Lock Recognition", frame)
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    run_system()
