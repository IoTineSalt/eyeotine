import paho.mqtt.client as mqtt
import json

# This is the Publisher



client = mqtt.Client()
client.max_queued_messages_set(0)
client.connect("localhost",1883,60)
client.loop_start()
data = {"user": "test", "data":{"text":"Good morning", "day":"Friday"}}
json_string = json.dumps(data)
for i in range(10):
    data = {"user": "test", "data":{"text":"Good morning", "day":"Friday" + str(i)}}
    json_string = json.dumps(data)
    print(client.publish("visualisation", json_string, qos=2))
client.loop_stop()
client.disconnect()
