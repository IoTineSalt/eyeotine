import socket
from paho.mqtt.client import Client
import select
import logging
import mqtt_lib
import esp_lib
import argparse
from queue import Queue

parser = argparse.ArgumentParser(description='EP-MQTT-Server')
parser.add_argument('--ipaddr', '-ip', type=str, default='0.0.0.0',
                    help='IP address for MQTT and EP')
parser.add_argument('--mqtthost','-mh',type=str, help='MQTT Server by hostname')
parser.add_argument('--epport', '-ep', type=int, default=2021,
                    help='EP Port')
parser.add_argument('--mqttport', '-mp', type=int, default=1883,
                    help='MQTT Port')
parser.add_argument('--log','-l', nargs='+', type=str, default=["INFO"])
args = parser.parse_args()

# define log level
logging.basicConfig(level=args.log[0])
# Socket bindings
esp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP

# mqqtserver runs on other hostname
if args.mqtthost:
    mqtt_ipaddr = socket.gethostbyname(args.mqtthost)
else:
    mqtt_ipaddr = args.ipaddr
esp_sock.bind((args.ipaddr, args.epport))
logging.info("bind esp socket")


mqtt_client = Client()
mqtt_client.on_connect = mqtt_lib.on_connect
mqtt_client.on_message = mqtt_lib.on_message
mqtt_client.connect(mqtt_ipaddr, args.mqttport, keepalive=60)

mqtt_sock = mqtt_client.socket()
logging.info("connected to broker")


# MQTT subscriptions
MQTT_TOPIC = [("ota", 2),("tracker", 2),("server/camera_config", 2)]
rc, _ = mqtt_client.subscribe(MQTT_TOPIC)
if rc > 0:
    logging.error("no subscription possible")

#DataCollector
data_collector = esp_lib.DataCollector(mqtt_client)


# input and output sockets
r_socks = [mqtt_sock, esp_sock]

esp_write_queue = Queue()

esp_lib.initialize_esp(esp_write_queue, mqtt_client)
mqtt_lib.initialize_mqtt(mqtt_client, esp_lib.get_esp_list())

inputs = []
outputs = []

# main select loop
while r_socks:
    w_socks = []
    if mqtt_client.want_write():
        w_socks += [mqtt_sock]
    if not esp_write_queue.empty():
        w_socks += [esp_sock]
    inputs, outputs, errors = select.select(r_socks, w_socks, r_socks, 0.01)
    if len(errors)> 0:
        logging.error("Socket error while executing select")

    if mqtt_sock in inputs:
        rc = mqtt_client.loop_read()
        mqtt_lib.read()
        if rc or mqtt_sock is None:
            logging.error("mqtt read error")


    if mqtt_sock in outputs:
        rc = mqtt_client.loop_write()
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

    mqtt_client.loop_misc()
    data_collector()
