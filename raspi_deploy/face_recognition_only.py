import cv2
import json
import numpy as np
import base64
import os
import time

# ================== CONFIG ==================
# Tu dong lay duong dan cung thu muc voi file script nay
DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "db.json")

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
FACE_SIZE = (200, 200)      # Kich thuoc chuan hoa khuon mat

# ================== DATABASE ==================
label_map = {}        # {0: "Dr. Quang", 1: "Dr. An", ...}
is_trained = False

def load_database():
    """Doc db.json, crop mat, train LBPH recognizer."""
    global label_map, is_trained

    if not os.path.exists(DB_FILE):
        print("[DB] File db.json khong ton tai!")
        return

    try:
        print("[DB] Dang tai co so du lieu khuon mat...")
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

                # Tang cuong du lieu (Data Augmentation)
                faces.append(face_resized)
                labels.append(label_id)

                faces.append(cv2.flip(face_resized, 1))
                labels.append(label_id)

                bright = cv2.convertScaleAbs(face_resized, alpha=1.2, beta=20)
                faces.append(bright)
                labels.append(label_id)

                dark = cv2.convertScaleAbs(face_resized, alpha=0.8, beta=-20)
                faces.append(dark)
                labels.append(label_id)

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
        print(f"[AI] Label={label}, Confidence={confidence:.1f} (threshold={CONFIDENCE_THRESHOLD})")

        if confidence < CONFIDENCE_THRESHOLD and label in label_map:
            name = label_map[label]
            similarity = min(99.9, max(0.0, 100 - (confidence / 2)))
            return name, (x, y, x + w, y + h), round(similarity, 1)
    except Exception as e:
        print(f"[AI] Loi predict: {e}")

    return "Unknown", (x, y, x + w, y + h), 0.0


# ================== MAIN ==================
def run_recognition():
    """Vong lap chinh: Mo camera va nhan dien lien tuc."""
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, RESIZE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 360)

    if not cap.isOpened():
        print("[ERROR] Khong mo duoc camera! Kiem tra USB Webcam.")
        return

    # Load database va train model
    load_database()

    print("=" * 50)
    print("  NHAN DIEN KHUON MAT ")
    print(f"  Nguong tin cay : {CONFIDENCE_THRESHOLD}")
    print(f"  Xac nhan       : {CONFIRM_FRAMES} frame lien tiep")
    print("  Phim: Q = Thoat")
    print("=" * 50)

    frame_count = 0
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

            # In ket qua khi du frame xac nhan
            if confirm_counter >= CONFIRM_FRAMES:
                print(f"\n>>> XAC NHAN: {result} (Do khop: {confidence}%) <<<\n")
                confirm_counter = 0

            elif result == "Unknown" and box is not None:
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

        cv2.imshow("Nhan Dien Khuon Mat (Doc Lap)", frame)
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    run_recognition()
