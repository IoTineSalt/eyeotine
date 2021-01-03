import socket
from paho.mqtt.client import Client
import select
import logging
import mqtt
import esp
import argparse

parser = argparse.ArgumentParser(description='EP-MQTT-Server')
parser.add_argument('--ipaddr', '-ip', type=str, default='0.0.0.0',
                    help='IP address for MQTT and EP')
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
esp_sock.bind((args.ipaddr, args.epport))
esp_sock.setblocking(0)
logging.info("bind esp socket")


mqtt_client = Client()
mqtt_client.on_connect = mqtt.on_connect
mqtt_client.connect(args.ipaddr, args.mqttport, 60)

mqtt_sock = mqtt_client._sock
mqtt_sock.setblocking(0)
mqtt_sock_pairR = mqtt_client._sockpairR
mqtt_sock_pairR.setblocking(0)
logging.info("connected to broker")


# MQTT subscriptions
MQTT_TOPIC = [("ota",2),("tracker",2),("change_settings",2)]
rc, _ = mqtt_client.subscribe(MQTT_TOPIC)
if rc > 0:
    logging.error("no subscription possible")


# MQTT client add callbacks


# input and output sockets
r_socks = [esp_sock, mqtt_sock, mqtt_sock_pairR]
w_socks = [esp_sock, mqtt_sock]

print(r_socks, w_socks)

inputs = []
outputs = []

# main select loop
while r_socks:
    #TODO: SELECT SHOULD BLOCK BUT DOESN'T DO!
    inputs, outputs, errors = select.select(r_socks, w_socks, r_socks)
    print(inputs, outputs, errors)
    if len(errors)> 0:
        logging.error("Socket error while executing select")


    if mqtt_sock in inputs:
        rc = mqtt_client.loop_read(1)
        if rc or mqtt_sock is None:
            logging.error("mqtt read error")

    if mqtt_sock_pairR in inputs:
        # Stimulate output write even though we didn't ask for it, because
        # at that point the publish or other command wasn't present.
        outputs.insert(0, mqtt_sock)
        # Clear sockpairR - only ever a single byte written.
        try:
            mqtt_sock_pairR.recv(1)
        except socket.error as err:
            if err.errno != EAGAIN:
                logging.error("mqtt socket pairR error")

    if mqtt_sock in outputs:
        rc = mqtt_client.loop_write(1)
        if rc or mqtt_sock is None:
            logging.error("mqtt write error")
