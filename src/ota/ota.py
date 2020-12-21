#! /usr/bin/env python
# -*- coding: utf-8 -*-
from paho.mqtt.client import Client
import argparse

parser = argparse.ArgumentParser(description='OTA-Updater')
parser.add_argument('filename', type=str, help='filename to new binary')
parser.add_argument('--host', '-H', type=str, default='localhost',
                    help='host IP from the mqtt broker')
parser.add_argument('--port', '-p', type=int, default=1883,
                    help='host port')
parser.add_argument('--topic', '-t', type=str, default='ota')
args = parser.parse_args()

client = Client()
client.connect(args.host, args.port, 60)
client.loop_start()
with open(args.filename, 'rb') as file:
    data = file.read()
    return_value = client.publish(args.topic, data, qos=2)
    if return_value[1] != 1:
        exit(-1)
