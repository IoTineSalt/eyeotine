import logging
def on_connect(client, userdata, flags, rc, properties=None):
    if rc >0:
        logging.error("connect error")
    else:
        logging.info("connected to broker")

def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
