import ssl
from http.server import BaseHTTPRequestHandler, HTTPServer
from paho.mqtt.client import Client
from queue import Queue
import logging

logging.basicConfig(level="INFO")
payload = None
def on_connect(client, userdata, flags, rc, properties=None):
    if rc >0:
        logging.error("connect error")
    else:
        logging.info("connected to broker")

def on_message(client, userdata, msg):
    global payload
    if msg.topic == 'ota':
        logging.info("receive ota config")
        payload =  msg.payload

class FileServer(BaseHTTPRequestHandler):

    def do_GET(self):
        global payload
        self.send_response(200)
        self.send_header('Content-type', 'application/otafirmware')
        self.send_header('Content-Disposition', 'attachment; filename="ota.firm"')
        self.end_headers()
        logging.info("get request")
        self.wfile.write(payload)

mqtt_client = Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect("broker", 1883, keepalive=60)
mqtt_client.loop_start()
mqtt_client.subscribe(("ota", 2))
logging.info("subscribed to ota")

httpd = HTTPServer(('ota_webserver', 80), FileServer)
logging.info("start http server")
httpd.serve_forever()
httpd.server_close()


# httpsd = HTTPServer(('localhost', 4443), FileServer)
# httpsd.socket = ssl.wrap_socket(httpsd.socket, certfile='./server_certs/ca_cert.pem', server_side=True)
# logging.info("start https server")
# httpsd.serve_forever()
# httpsd.server_close()
