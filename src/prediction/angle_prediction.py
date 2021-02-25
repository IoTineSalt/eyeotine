from PIL import Image,ImageFile
from yolov5 import YOLOv5
import numpy as np
import torch

###Setup YOLO###
# set model params
model_path = "yolov5s.pt" # it automatically downloads yolov5s model to given path

if torch.cuda.is_available():
    device = "cuda" # or "cpu"
else:
    device = "cpu"

# init yolov5 model
yolov5 = YOLOv5(model_path, device)


###DUMMY BYTE STREAM###
with open("human_crowd.jpeg","rb") as f:
    byte_string = f.read()

p = ImageFile.Parser()

p.feed(byte_string)

im = p.close()


###Prediction###

results = yolov5.predict(im)

centers = [np.array([float(x[0]), float(x[1])]) for x in results.xywhn[0] if x[5]==0]
# print("centers", centers)

fov = 80
angles = [center[0]*fov-(fov/2) for center in centers]
print("angles", angles)

results.save()

# load images
# image1 = Image.open("darknet/data/person.jpg")
# image2 = Image.open("darknet/data/dog.jpg")

# perform inference
# results = yolov5.predict(image1)

# perform inference with higher input size
# results = yolov5.predict(image1, size=1280)

# perform inference with test time augmentation
# results = yolov5.predict(image1, augment=True)

# perform inference on multiple images
# results = yolov5.predict([image1, image2], size=1280, augment=True)

# print(results.xywhn)
