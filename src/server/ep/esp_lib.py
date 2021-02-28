from queue import Queue
from enum import Enum
from struct import Struct
import re


esp_write_queue = []
esp_list = []

def initialize(write_queue):
    esp_write_queue = write_queue


def read(esp_sock):
    data, (addr, port) = esp_sock.recvfrom(1024)
    accepted = False
    for esp in esp_list:
        if addr == esp.addr:
            esp(data)
            accepted = True
    if accepted is False:
        esp_list.append(Esp(addr, port, esp_write_queue))
        esp_list[-1](data)



# Enumsc
from dataclasses import dataclass

# ENUMS
class States(Enum):
    #Werte muessen moeglicherweise nochmals geaendert werden
    IDLE = 0
    ASSOCIATED = 1
    SYNCED = 2
    CAMERA_OFF = 3
    CAMERA_ON = 4

# global helper functions
def set_version(packet):






class Esp:
    def __init__(self, addr, port, write_queue):
        self.addr = addr # this should be a string
        self.port = port # this should be a port
        self.timestamp = 0
        sel.state = States.IDLE
        self.data_queue = Queue()
        self.write_queue = write_queue # in this queue there should be tuples of (data, (addr, port))


    def read(self, data):
        # IF

    def write(self, data):
        self.write_queue.put((data, (self.addr, self.port)))

    def restart(self):
        pass

    def on_connect(self):
        pass

    def send_config(self):
        pass

    def on_disconnect(self):
        esp_list.remove(self)




    __call__ = read
