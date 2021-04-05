import numpy as np
from paho.mqtt.client import Client
from bokeh.driving import count
from bokeh.layouts import column, gridplot, row
from bokeh.models import ColumnDataSource, Select, Slider
from bokeh.plotting import curdoc, figure
import logging
import json

# define log level
logger = logging.getLogger('bokeh')

source = ColumnDataSource(dict(
    x=[], y=[]
))


def on_connect(client, userdata, flags, rc, properties=None):
    if rc >0:
        logger.error("connect error")
    else:
        logger.info("connected to broker")

def on_message(client, userdata, msg):
    logging.info("receive data")
    # payload format:
    # {'x':[2,3,4], 'y':[2,3,4]}
    topic, payload = msg.topic, msg.payload
    json_dct = json.loads(payload)
    source.stream(json_dct, len(json_dct['x']))



mqtt_client = Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect("broker", 1883, keepalive=60)

mqtt_client.subscribe(("visualisation", 2))
logger.info("Subscribed")
p = figure(plot_height=500)


p.circle(x='x', y='y', source=source)



def update():
    mqtt_client.loop(timeout=0.05)


curdoc().add_root(column(p))
curdoc().add_periodic_callback(update, 50)
curdoc().title = "Tracker"
