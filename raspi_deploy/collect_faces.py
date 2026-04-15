

import cv2
import json
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

# ================== SETTINGS ==================
RESIZE_WIDTH = 480
FACE_SIZE = (200, 200)      # Kich thuoc chuan hoa khuon mat

# ================== DATABASE ==================
def load_db():
    """Doc db.json hien tai."""
    if os.path.exists(DB_FILE):
        try:
            with open(DB_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except:
            return {}
    return {}

def save_db(db):
    """Ghi db.json."""
    with open(DB_FILE, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=4, ensure_ascii=False)


def capture_face(name):
    """Mo camera, phat hien mat, chup va luu vao db.json."""
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, RESIZE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 360)

    if not cap.isOpened():
        print("[ERROR] Khong mo duoc camera! Kiem tra USB Webcam.")
        return False

    print("=" * 50)
    print(f"  Thu thap khuon mat cho: {name}")
    print("  Phim: S = Chup luu  |  Q = Thoat")
    print("=" * 50)

    saved = False

    # Tao cua so truoc vong lap de dam bao nhan focus ban phim
    window_name = "Thu Thap Khuon Mat"
    cv2.namedWindow(window_name, cv2.WINDOW_AUTOSIZE)
    print("[INFO] Click chuot vao cua so camera roi nhan S/Q.")

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[CAM] Khong doc duoc frame!")
            break

        frame = cv2.resize(frame, (RESIZE_WIDTH, 360))
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        # Phat hien khuon mat
        faces = face_cascade.detectMultiScale(
            gray, scaleFactor=1.1, minNeighbors=5, minSize=(60, 60)
        )

        for (x, y, w, h) in faces:
            cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(frame, "Nhan S de chup", (x, y - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

        cv2.imshow(window_name, frame)
        key = cv2.waitKey(10) & 0xFF

        if key == ord('s') or key == ord('S'):
            if len(faces) == 0:
                print("[!] Khong phat hien khuon mat nao. Hay dua mat vao camera.")
                continue

            # Lay mat lon nhat
            (x, y, w, h) = max(faces, key=lambda r: r[2] * r[3])
            face_roi = frame[y:y+h, x:x+w]
            face_resized = cv2.resize(face_roi, FACE_SIZE)

            # Encode anh thanh base64
            _, buffer = cv2.imencode('.jpg', face_resized)
            img_base64 = "data:image/jpeg;base64," + base64.b64encode(buffer).decode('utf-8')

            # Luu vao db.json
            db = load_db()
            db[name] = {
                "registered_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "access_count": 0,
                "avatar": img_base64[:100] + "...",
                "full_image": img_base64
            }
            save_db(db)

            print(f"[OK] Da luu khuon mat cua '{name}' vao {DB_FILE}")
            saved = True
            break

        elif key == ord('q') or key == ord('Q'):
            print("[EXIT] Thoat khong luu.")
            break

    cap.release()
    cv2.destroyAllWindows()
    return saved


def list_users():
    """Liet ke danh sach nguoi dung da dang ky."""
    db = load_db()
    if not db:
        print("[DB] Chua co nguoi dung nao duoc dang ky.")
        return

    print("\n" + "=" * 40)
    print("  DANH SACH NGUOI DUNG DA DANG KY")
    print("=" * 40)
    for i, (name, info) in enumerate(db.items(), 1):
        reg = info.get("registered_at", "N/A")
        has_img = "Co" if info.get("full_image") else "Khong"
        print(f"  {i}. {name}  |  Ngay DK: {reg}  |  Anh: {has_img}")
    print("=" * 40 + "\n")


def delete_user(name):
    """Xoa nguoi dung khoi db.json."""
    db = load_db()
    if name in db:
        del db[name]
        save_db(db)
        print(f"[OK] Da xoa '{name}' khoi CSDL.")
    else:
        print(f"[!] Khong tim thay '{name}' trong CSDL.")


# ================== MAIN ==================
if __name__ == "__main__":
    print("\n" + "=" * 50)
    print("  CONG CU THU THAP KHUON MAT - FaceGuard")
    print("=" * 50)

    while True:
        print("\nChon chuc nang:")
        print("  1. Dang ky khuon mat moi")
        print("  2. Xem danh sach nguoi dung")
        print("  3. Xoa nguoi dung")
        print("  0. Thoat")
        choice = input("\nNhap lua chon (0-3): ").strip()

        if choice == "1":
            name = input("Nhap ten nguoi dung (VD: Dr. Quang): ").strip()
            if name:
                capture_face(name)
            else:
                print("[!] Ten khong duoc de trong.")

        elif choice == "2":
            list_users()

        elif choice == "3":
            name = input("Nhap ten nguoi dung can xoa: ").strip()
            if name:
                delete_user(name)

        elif choice == "0":
            print("[EXIT] Tam biet!")
            break

        else:
            print("[!] Lua chon khong hop le.")
