#! /usr/bin/env python
# -*- coding: utf-8 -*-
from paho.mqtt.client import Client
import argparse
import json
import base64

parser = argparse.ArgumentParser(description='prediction_tester')
parser.add_argument('im', type=str, help='image path')
parser.add_argument('--host', '-H', type=str, default='localhost',
                    help='host IP from the mqtt broker')
parser.add_argument('--port', '-p', type=int, default=1883,
                    help='host port')
parser.add_argument('--topic', '-t', type=str, default='server/images/data')
args = parser.parse_args()

client = Client()
client.connect(args.host, args.port, 60)
client.loop_start()
with open(args.im, 'rb') as file:
    data = file.read()
    msg = {}
    msg['data'] = base64.b64encode(data).decode('ascii')
    msg['timestamp'] = 324783
    msg['source_ipaddr'] = '172.0.0.21'
    msg_json = json.dumps(msg)
    return_value = client.publish(args.topic, msg_json, qos=2)
    if return_value[1] != 1:
        exit(-1)
client.loop_stop()
client.disconnect()
