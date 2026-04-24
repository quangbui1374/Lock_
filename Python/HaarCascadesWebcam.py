import cv2

webcam = cv2.VideoCapture(1)
face_cascade = cv2.CascadeClassifier(r'F:\WEB IOT\Python\opencv\data\haarcascades\haarcascade_frontalface_default.xml')

while True:
    ret, frame = webcam.read()
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    cv2.imshow('frame', gray)
    faces = face_cascade.detectMultiScale(gray, 1.3, 5)

    for (x, y, w, h) in faces:
        cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)

    cv2.imshow("quang", frame);
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break
cv2.destroyAllWindows()
webcam.release()

