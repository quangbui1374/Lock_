import cv2
import numpy as np
import os


recog = cv2.face.LBPHFaceRecognizer_create()
recog.read("trainer/traine.yml")

faceCascade = cv2.CascadeClassifier(r'F:\WEB IOT\Python\opencv\data\haarcascades\haarcascade_frontalface_default.xml')


font = cv2.FONT_HERSHEY_SIMPLEX
id = 0
names = ['0', '1', '2', '3', '4']

cam = cv2.VideoCapture(1)
cam.set(3, 640)
cam.set(4, 480)

minW = 0.1* cam.get(3)
minH = 0.1* cam.get(4)

while True:
    ret, img = cam.read()
    img = cv2.flip(img, 1)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    faces = faceCascade.detectMultiScale(
        gray,
        scaleFactor=1.2,
        minNeighbors=5,
        minSize=(int(minW), int(minH))
    )

    for (x, y, w, h) in faces:
        cv2.rectangle(img, (x, y), (x + w, y + h), (255, 0, 0), 2)

        id, conf = recog.predict(gray[y:y + h, x:x + w])

        if(conf < 100):
            id=names[id]
            conf = " {0}%".format(round(100 - conf))
        else:
            id = "unknown "
            conf = " {0}%".format(round(100 - conf))

        cv2.putText(img, str(id), (x - 10, y - 10), font, 1, (255, 255, 255), 2)
        cv2.putText(img, str(conf), (x + 10, y - 10), font, 1, (255, 255, 255), 2)

    cv2.imshow('face', img)

    k = cv2.waitKey(1) & 0xFF
    if k == 27:
        break
print("\n Thoat")
cam.release()
cv2.destroyAllWindows()