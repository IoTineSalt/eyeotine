from PIL import Image,ImageFile
from yolov5 import YOLOv5
import numpy as np
import torch
import logging
from queue import Queue
from paho.mqtt.client import Client
import json
import base64

# define log level
logging.basicConfig(level="INFO")

mqtt_msg_queue = Queue()

def on_connect(client, userdata, flags, rc, properties=None):
    if rc >0:
        logging.error("connect error")
    else:
        logging.info("connected to broker")

def on_message(client, userdata, msg):
    logging.info("receive image")
    mqtt_msg_queue.put((msg.topic, msg.payload))



###Setup YOLO###
# set model params
model_path = "./yolov5s.pt" # it automatically downloads yolov5s model to given path

if torch.cuda.is_available():
    logging.info("gpu avaible")
    device = "cuda" # or "cpu"
else:
    logging.info("no gpu avaible")
    device = "cpu"

# init yolov5 model
yolov5 = YOLOv5(model_path, device)

mqtt_client = Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect("broker", 1883, keepalive=60)
mqtt_client.loop_start()
mqtt_client.subscribe(("server/images/data", 2))
logging.info("Subscribed")

p = ImageFile.Parser()
while True:
    try:
        topic, data = mqtt_msg_queue.get()
        data = json.loads(data)
    except:
        continue

    p.feed(base64.b64decode(data['data']))
    try:
        im = p.close()
    except OSError:
        logging.error("the image format is not correct")
        continue
    im.filename='dummyfilename'

    ###Prediction###

    results = yolov5.predict(im)
    logging.info("predicted" + ' at '+ str(data['timestamp']) + ' from ' +\
      str(data['source_ipaddr']))
    logging.info(str(results.xywhn[0]))
    centers = [np.array([float(x[0]), float(x[1])]) for x in results.xywhn[0] if x[5]==0]
    # print("centers", centers)

    fov = 80
    angles = [center[0]*fov-(fov/2) for center in centers]
    logging.info("calculated angles" +  str(angles))
    msg = {}
    msg["timestamp"] = data['timestamp']
    msg['source_ipaddr'] = data['source_ipaddr']
    msg['angles'] = angles
    msg_json = json.dumps(msg)
    return_value = mqtt_client.publish('tracker/data', msg_json, qos=2)
    if return_value[1] == 0 :
        logging.error("error while publishing" + str(return_value))


mqtt_client.loop_stop()
mqtt_client.disconnect()
