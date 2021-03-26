import socket
from paho.mqtt.client import Client
import select
import logging
import mqtt
import esp_lib
import argparse
from queue import Queue

parser = argparse.ArgumentParser(description='EP-MQTT-Server')
parser.add_argument('--ipaddr', '-ip', type=str, default='0.0.0.0',
                    help='IP address for MQTT and EP')
parser.add_argument('--epport', '-ep', type=int, default=58631,
                    help='EP Port')
parser.add_argument('--mqttport', '-mp', type=int, default=1883,
                    help='MQTT Port')
parser.add_argument('--log','-l', nargs='+', type=str, default=["INFO"])
args = parser.parse_args()

# define log level
logging.basicConfig(level=args.log[0])

# Socket bindings
esp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP
esp_sock.bind((args.ipaddr, args.epport))
logging.info("bind esp socket")


mqtt_client = Client()
mqtt_client.on_connect = mqtt.on_connect
mqtt_client.on_message = mqtt.on_message
mqtt_client.connect(args.ipaddr, args.mqttport, 60)

mqtt_sock = mqtt_client.socket()
logging.info("connected to broker")


# MQTT subscriptions
MQTT_TOPIC = [("ota",2),("tracker",2),("change_settings",2)]
rc, _ = mqtt_client.subscribe(MQTT_TOPIC)
if rc > 0:
    logging.error("no subscription possible")


# MQTT client add callbacks


# input and output sockets
r_socks = [mqtt_sock, esp_sock]

esp_write_queue = Queue()

esp_lib.initialize(esp_write_queue)

inputs = []
outputs = []

# main select loop
while r_socks:
    w_socks = []
    if mqtt_client.want_write():
        w_socks += [mqtt_sock]
    if not esp_write_queue.empty():
        w_socks += [esp_sock]
    inputs, outputs, errors = select.select(r_socks, w_socks, r_socks, 0.05)
    if len(errors)> 0:
        logging.error("Socket error while executing select")

    if mqtt_sock in inputs:
        rc = mqtt_client.loop_read()
        if rc or mqtt_sock is None:
            logging.error("mqtt read error")

    if mqtt_sock in outputs:
        rc = mqtt_client.loop_write(1)
        if rc or mqtt_sock is None:
            logging.error("mqtt write error")



    if esp_sock in inputs:
        rc = esp_lib.read(esp_sock, esp_write_queue)
        if rc != 0:
            logging.error("esp socket read error")

    # update esp time
    esp_lib.update_time_and_ping()

    if esp_sock in outputs:
        while not esp_write_queue.empty():
            msg = esp_write_queue.get()
            esp_sock.sendto(*msg)
