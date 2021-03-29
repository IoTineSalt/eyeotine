import logging
from queue import Queue

mqtt_read_queue = Queue()

esp_list = []
mqtt_client = None

def initialize_mqtt(client, esps):
    global esp_list
    global mqtt_client
    mqtt_client = client
    esp_list = esps

def on_connect(client, userdata, flags, rc, properties=None):
    if rc >0:
        logging.error("connect error")
    else:
        logging.info("connected to broker")

def on_message(client, userdata, msg):
    mqtt_read_queue.put((msg.topic, msg.payload))


def read():
    while not mqtt_read_queue.empty():
        topic, payload =  mqtt_read_queue.get()
        if topic == 'ota':
            logging.info("get ota command")
            for esp in esp_list:
                esp.send_ota(payload)

        if topic == 'server/camera_config':
            logging.info("receive camera config")
            for esp in esp_list:
                esp.send_config(payload)
        #hier muss alles hin, was getan werden soll, bei welchen Nachrichten
