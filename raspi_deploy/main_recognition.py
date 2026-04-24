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
CONFIDENCE_THRESHOLD = 55   # Giam tu 80 xuong 55 (cang thap cang chat che)
FRAME_SKIP = 3              # Xu ly moi N frame (giam tai cho Pi)
RESIZE_WIDTH = 480
CONFIRM_FRAMES = 5          # Phai dung mat dien tiep 5 frame moi mo cua (chong nhan dien nang quay/nhay)
COOLDOWN = 5                # Giay giua 2 lan gui cung 1 nguoi
FACE_SIZE = (200, 200)      # Kich thuoc chuan hoa khuon mat


# ================== DATABASE ==================
label_map = {}        # {0: "Dr. Quang", 1: "Dr. An", ...}
last_db_mtime = 0
is_trained = False

def _decode_and_extract_face(b64_str):
    """Giai ma 1 anh base64, phat hien mat, tra ve face ROI (grayscale, resized) hoac None."""
    try:
        if "," in b64_str:
            b64_str = b64_str.split(",")[-1]
        img_data = base64.b64decode(b64_str)
        nparr = np.frombuffer(img_data, np.uint8)
        img_cv2 = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

        if img_cv2 is None:
            return None

        gray = cv2.cvtColor(img_cv2, cv2.COLOR_BGR2GRAY)

        detected = face_cascade.detectMultiScale(
            gray, scaleFactor=1.1, minNeighbors=5, minSize=(60, 60)
        )

        if len(detected) == 0:
            return None

        # Lay mat lon nhat
        (x, y, w, h) = max(detected, key=lambda r: r[2] * r[3])
        face_roi = gray[y:y+h, x:x+w]
        face_resized = cv2.resize(face_roi, FACE_SIZE)
        return face_resized
    except Exception:
        return None


def _augment_face(face_resized):
    """Tao bien the tu 1 khuon mat de tang do chinh xac. Tra ve list cac anh."""
    augmented = [face_resized]

    # Lat ngang
    augmented.append(cv2.flip(face_resized, 1))

    # Tang do sang
    augmented.append(cv2.convertScaleAbs(face_resized, alpha=1.2, beta=20))

    # Giam do sang
    augmented.append(cv2.convertScaleAbs(face_resized, alpha=0.8, beta=-20))

    # Lam mo nhe (mo phong camera chat luong thap)
    augmented.append(cv2.GaussianBlur(face_resized, (3, 3), 0))

    # Can bang histogram (tang tuong phan)
    augmented.append(cv2.equalizeHist(face_resized))

    return augmented


def load_database():
    """Doc db.json, crop mat tu NHIEU anh, train LBPH recognizer."""
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
            if not isinstance(info, dict):
                continue

            # Lay danh sach anh: uu tien face_images (multi), fallback full_image (single)
            image_list = info.get("face_images", [])
            if not image_list:
                single = info.get("full_image", "")
                if single:
                    image_list = [single]

            if not image_list:
                continue

            person_face_count = 0

            for img_idx, b64_str in enumerate(image_list):
                if not b64_str:
                    continue

                face_resized = _decode_and_extract_face(b64_str)
                if face_resized is None:
                    print(f"[DB] Anh {img_idx+1}: Khong tim thay mat - {name}")
                    continue

                # Augment moi anh goc
                variants = _augment_face(face_resized)
                for v in variants:
                    faces.append(v)
                    labels.append(label_id)
                    person_face_count += 1

            if person_face_count > 0:
                new_label_map[label_id] = name
                label_id += 1
                print(f"[DB] Da hoc mat: {name} ({len(image_list)} anh goc -> {person_face_count} mau)")
            else:
                print(f"[DB] Khong co mat hop le: {name}")

        if len(faces) > 0:
            recognizer.train(faces, np.array(labels))
            label_map = new_label_map
            is_trained = True
            last_db_mtime = current_mtime
            print(f"[DB] Hoan tat! Da hoc {len(label_map)} nguoi dung ({len(faces)} mau tong cong)")
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
