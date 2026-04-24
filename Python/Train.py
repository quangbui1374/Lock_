import cv2
import numpy as np
from PIL import Image
import os


path = 'Datasheet'

recog = cv2.face.LBPHFaceRecognizer_create()
detect = cv2.CascadeClassifier(r'F:\WEB IOT\Python\opencv\data\haarcascades\haarcascade_frontalface_default.xml')

def getImagesAndLabels(path):

    imagePaths = [os.path.join(path, f) for f in os.listdir(path)]
    faceSamples = []
    ids = []

    for imagePath in imagePaths:

        PIL_img = Image.open(imagePath).convert('L')
        img_numpy = np.array(PIL_img, 'uint8')

        id = int(os.path.split(imagePath)[-1].split('.')[0].replace("User", ""))
        face = detect.detectMultiScale(img_numpy)

        for (x, y, w, h) in face:
            faceSamples.append(img_numpy[y:y+h, x:x+w])
            ids.append(id)

    return faceSamples, ids
print("\n Dang train du lieu...")
faces, ids = getImagesAndLabels(path)
recog.train(faces, np.array(ids))

recog.write('trainer/traine.yml')
print("\n [INFO] {0} Khuon mat da duoc train ".format(len(np.unique(ids))))
