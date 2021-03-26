from queue import Queue
from enum import Enum
from construct import *
import re
import time
import logging
logging.basicConfig(level="INFO")

# esp_write_queue = []
esp_list = []

def initialize(write_queue):
    esp_write_queue = write_queue

def update_time_and_ping():
    ms = time.time_ns() // 1000000
    for esp in esp_list:
        esp.timestamp = ms
        esp.ping()


def read(esp_sock, esp_write_queue):
    data, (addr, port) = esp_sock.recvfrom(1024)
    accepted = False
    for esp in esp_list:
        if addr == esp.addr and port == esp.port:
            esp(data)
            accepted = True
    if accepted is False:
        esp_list.append(Esp(addr, port, esp_write_queue))
        esp_list[-1](data)

    return 0


# STRUCTS
# Header = CStruct("struct header{uint8_t ver_type_subtype;};")
Header = Struct(
    "ver_type_subtype" / Int8ub,
)

HeaderWithFlags = Struct(
    "header" / Header,
    "flags" / Int8ub,
)

# ENUMS
class States(Enum):
    #Werte muessen moeglicherweise nochmals geaendert werden
    IDLE = 0
    ASSOCIATED = 1
    SYNCED = 2
    CAMERA_OFF = 3
    CAMERA_ON = 4

class Types(Enum):
    TYPE_MGMT = 0
    TYPE_CTRL = 1
    TYPE_DATA = 2

class SubtypesMgmt(Enum):
    SUBTYPE_MGMT_ASSOC = 0
    SUBTYPE_MGMT_DASSOC =  1
    SUBTYPE_MGMT_PING = 2

class SubtypeCtrl(Enum):
    SUBTYPE_CTRL_RESTART = 0
    SUBTYPE_CTRL_SYNC = 1
    SUBTYPE_CTRL_CONFIG = 2

class SubtypeData(Enum):
    SUBTYPE_DATA_IMAGE = 0

# global helper functions
def set_version(packet, value):
    packet.ver_type_subtype = (packet.ver_type_subtype & 0x3F) | ((value & 0x03) << 6)

def set_type(packet, value):
    packet.ver_type_subtype = (packet.ver_type_subtype & 0xCF) | ((value & 0x03) << 4)

def set_subtype(packet, value):
    packet.ver_type_subtype = (packet.ver_type_subtype & 0xF0) | (value & 0x0F)

def set_flag(packet, value):
    packet.flags = (packet.flags & 0x01) | (value & 0x01)

def get_version(packet):
    ver_byte = packet.ver_type_subtype
    version = (ver_byte & 0xC0) >> 6
    return version

def get_type(packet):
     return (packet.ver_type_subtype & 0x30) >> 4

def get_subtype(packet):
    return packet.ver_type_subtype & 0x0F




# print(Header.build(dict(ver_type_subtype=22)))
# testheader = Header.parse(b'\0')
# testmessage = Message.parse(b'\0Hellowolrd')
# print(type(testheader.ver_type_subtype))
# set_version(testheader,2)
# print(Header.build(testheader))
# Philipp = Enum(Byte, a=5, b=6)
# print(Philipp)



class Esp:
    def __init__(self, addr, port, write_queue):
        self.addr = addr # this should be a string
        self.port = port # this should be a port
        self.timestamp = time.time_ns() // 1000000
        self.last_send_package_timestamp = 0
        self.last_recv_package_timestamp = 0
        self.state = States.IDLE
        self.data_queue = Queue()
        self.write_queue = write_queue # in this queue there should be tuples of (data, (addr, port))


    def read(self, data):
        self.last_recv_package_timestamp = time.time_ns() // 1000000
        header_bytes = data[0:1]
        header = Header.parse(header_bytes)
        if get_version(header) != 0:
            logging.error("false version")
            return
        if self.state == States.IDLE:
            logging.info("State IDLE")
            # ASSOCIATION
            if get_type(header) != Types.TYPE_MGMT:
                logging.error("false type. The header type should be TYPE_MGMGT")
                return
            if get_subtype(header) != SubtypesMgmt.SUBTYPE_MGMT_ASSOC:
                logging.error("false type. The subtype should be SUBTYPE_MGMT_ASSOC")
            association_response = HeaderWithFlags.parse(b'\0000')
            logging.info("Send association response")
            set_type(association_response.header, Types.TYPE_MGMT)
            set_subtype(association_response.header, SubtypesMgmt.SUBTYPE_MGMT_ASSOC)
            set_flag(association_response, 1)
            self.write(HeaderWithFlags.build(association_response))
            self.state =States.ASSOCIATED
        elif self.state == States.ASSOCIATED:
            logging.info("State ASSOCIATED")
            for esp in esp_list:
                esp.sync()
            self.state = States.SYNCED
        else:
            # state SYNCED
            if get_type(header) == Types.TYPE_MGMT:
                if get_subtype(header) == SubtypesMgmt.SUBTYPE_MGMT_PING:
                    logging.info("receive ping")
                if get_subtype(header) == SubtypesMgmt.SUBTYPE_MGMT_ASSOC:
                    loffing.error("should not receive associating request")



    def ping(self):
        if self.timestamp - self.last_recv_package_timestamp > 1000 \
            and self.timestamp - self.last_send_package_timestamp >=900:
            ping = HeaderWithFlags.parse(b'\0000')
            set_type(ping.header, Types.TYPE_MGMT)
            set_subtype(ping.header, SubtypesMgmt.SUBTYPE_MGMT_PING)
            set_flag(ping, 0)
            self.write(HeaderWithFlags.build(ping))
            logging.info("send ping")
        if self.timestamp - self.last_recv_package_timestamp > 5000:
            self.on_disconnect()

    def sync(self):
        sync_request = HeaderWithFlags.parse(b'\0000')
        set_type(sync_request.header, Types.TYPE_CTRL)
        set_subtype(sync_request.header, SubtypeCtrl.SUBTYPE_CTRL_SYNC)
        set_flag(sync_request, 0)
        self.write(HeaderWithFlags.build(sync_request))
        logging.info("Send sync")

    def write(self, data):
        self.last_send_package_timestamp = time.time_ns() / 1000000
        self.write_queue.put((data, (self.addr, self.port)))

    def restart(self):
        restart_request = HeaderWithFlags.parse(b'\0000')
        set_type(restart_request.header, Types.TYPE_CTRL)
        set_subtype(restart_request.header, SubtypeCtrl.SUBTYPE_CTRL_RESTART)
        set_flag(restart_request, 0)
        self.write(HeaderWithFlags.build(restart_request))
        logging.info("Send restart request")

    def on_connect(self):
        pass

    def send_config(self):
        pass

    def on_disconnect(self):
        disassociation_request = HeaderWithFlags.parse(b'\0000')
        set_type(disassociation_request.header, Types.TYPE_MGMT)
        set_subtype(disassociation_request.header, SubtypesMgmt.SUBTYPE_MGMT_DASSOC)
        set_flag(disassociation_request, 0)
        self.write(HeaderWithFlags.build(disassociation_request))
        logging.info("Send disassociation")
        esp_list.remove(self)




    __call__ = read



# import socket
#
# UDP_IP = "127.0.0.1"
# UDP_PORT = 58631
#
# sock = socket.socket(socket.AF_INET, # Internet
#                      socket.SOCK_DGRAM) # UDP
# sock.bind((UDP_IP, UDP_PORT))
#
# while True:
#     data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
#     print("received message: %s" % data)
