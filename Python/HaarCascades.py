import numpy as np 
import cv2

face_cascade = cv2.CascadeClassifier(r'F:\WEB IOT\Python\opencv\data\haarcascades\haarcascade_frontalface_default.xml') 


img = cv2.imread(r'F:\WEB IOT\Python\nooo.jpg', cv2.IMREAD_UNCHANGED)
grayscale = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

faces = face_cascade.detectMultiScale(grayscale, 1.1, 4)

for x, y, w, h in faces:
    cv2.rectangle(img, (x, y), (x+w, y+h), (255, 0, 0), 2)
    roi_gray = grayscale[y:y+h, x:x+w]
    roi_color = img[y:y+h, x:x+w]

cv2.imshow("img", img)
cv2.waitKey(0)
cv2.destroyAllWindows()