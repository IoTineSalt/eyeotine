from queue import Queue
from enum import Enum
from construct import *
import re
import time
import logging
from collections import defaultdict
import base64
import json

logging.basicConfig(level="INFO")

# esp_write_queue = []
esp_list = []
esp_write_queue = None
mqtt_client = None

def initialize_esp(write_queue,mqtt_cli):
    global esp_write_queue
    esp_write_queue = write_queue

def get_esp_list():
    return esp_list

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

class SubtypeMgmt(Enum):
    SUBTYPE_MGMT_ASSOC = 0
    SUBTYPE_MGMT_DASSOC =  1
    SUBTYPE_MGMT_PING = 2

class SubtypeCtrl(Enum):
    SUBTYPE_CTRL_RESTART = 0
    SUBTYPE_CTRL_SYNC = 1
    SUBTYPE_CTRL_CONFIG = 2
    SUBTYPE_CTRL_OTA = 3

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
                esp_list.remove(self)
                return
            if get_subtype(header) != SubtypeMgmt.SUBTYPE_MGMT_ASSOC:
                logging.error("false type. The subtype should be SUBTYPE_MGMT_ASSOC")
                esp_list.remove(self)
                return
            association_response = HeaderWithFlags.parse(b'\0000')
            logging.info("Send association response")
            set_type(association_response.header, Types.TYPE_MGMT)
            set_subtype(association_response.header, SubtypeMgmt.SUBTYPE_MGMT_ASSOC)
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
                if get_subtype(header) == SubtypeMgmt.SUBTYPE_MGMT_PING:
                    logging.info("receive ping")
                if get_subtype(header) == SubtypeMgmt.SUBTYPE_MGMT_ASSOC:
                    logging.error("should not receive associating request")
                    esp_list.remove(self)
                    return
            if get_type(header) == Types.TYPE_DATA:
                if get_subtype(header) == SubtypeData.SUBTYPE_DATA_IMAGE:
                    self.data_queue.put(data[1:])
            if get_type(header) == Types.TYPE_MGMT:
                if get_subtype(header) == SubtypeMgmt.SUBTYPE_MGMT_DASSOC:
                    esp_list.remove(self)

    def ping(self):
        if self.timestamp - self.last_send_package_timestamp >= 1000:
            ping = HeaderWithFlags.parse(b'\0000')
            set_type(ping.header, Types.TYPE_MGMT)
            set_subtype(ping.header, SubtypeMgmt.SUBTYPE_MGMT_PING)
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
        set_version(restart_request.header, 0)
        set_type(restart_request.header, Types.TYPE_CTRL)
        set_subtype(restart_request.header, SubtypeCtrl.SUBTYPE_CTRL_RESTART)
        set_flag(restart_request, 0)
        self.write(HeaderWithFlags.build(restart_request))
        logging.info("Send restart request")

    def send_ota(self, config):
        ota_request = HeaderWithFlags.parse(b'\0000')
        set_version(ota_request.header, 0)
        set_type(ota_request.header, Types.TYPE_CTRL)
        set_subtype(ota_request.header, SubtypeCtrl.SUBTYPE_CTRL_OTA)
        set_flag(ota_request, 0)
        self.write(HeaderWithFlags.build(ota_request))
        logging.info("Send ota request")

    def send_config(self, config):
        config_header = Header.parse(b'\x00\x00')
        set_version(config_header, 0)
        set_type(config_header, Types.TYPE_CTRL)
        set_subtype(config_header, SubtypeCtrl.SUBTYPE_CTRL_CONFIG)
        header_bytes = Header.build(config_header)
        self.write(header_bytes + config)
        logging.info("Send camera config")

    def on_disconnect(self):
        disassociation_request = HeaderWithFlags.parse(b'\0000')
        set_type(disassociation_request.header, Types.TYPE_MGMT)
        set_subtype(disassociation_request.header, SubtypeMgmt.SUBTYPE_MGMT_DASSOC)
        set_flag(disassociation_request, 0)
        self.write(HeaderWithFlags.build(disassociation_request))
        logging.info("Send disassociation")
        esp_list.remove(self)

    __call__ = read

###############################################################################
# Data Structs
ImageHeader = BitStruct(
    "flag" / BitsInteger(8),
    "timestamp" / BitsInteger(10),
    "fragment_number" / BitsInteger(6)
)

class AutoVivification(dict):
    """Implementation of perl's autovivification feature."""
    def __getitem__(self, item):
        try:
            return dict.__getitem__(self, item)
        except KeyError:
            value = self[item] = type(self)()
            return value

class DataCollector:
    def __init__(self, mqtt_client):
        self.data = AutoVivification({}) # dict with keys as ip addresses
        self.mqtt_client = mqtt_client
        self.timestamp = None

    def collect(self):
        self.timestamp = time.time_ns() / 1000000
        # add new data to data dict
        for esp in esp_list:
            if not esp.data_queue.empty():
                data = esp.data_queue.get()
                header = ImageHeader.parse(data)
                ip_addr = esp.addr
                fragment_number = header.fragment_number
                timestamp = header.timestamp
                imag_frag = data[3:]
                self.data[ip_addr][timestamp][fragment_number] = imag_frag
                if header.flag & 1 > 0:
                    self.data[ip_addr][timestamp]["finished"] = self.timestamp

        deletion_keys = []
        for ip_addr in self.data.keys():
            for timestamp in self.data[ip_addr].keys():
                if self.data[ip_addr][timestamp]["finished"]:
                    # if last package receive to old delete timestamp data
                    old_timestamp = self.data[ip_addr][timestamp]["finished"]
                    if self.timestamp - old_timestamp > 100:
                        logging.info("delete old timestamp")
                        deletion_keys.append((ip_addr, timestamp))
                        continue

                    # test if all fragments received then send image
                    addr_timestamp_dict = dict(self.data[ip_addr][timestamp])
                    del addr_timestamp_dict["finished"]
                    addr_timestamp_dict = dict(sorted(addr_timestamp_dict.items()))
                    keys = list(addr_timestamp_dict.keys())
                    highest_key = keys[-1]
                    comparision_list = list(range(highest_key + 1))
                    if comparision_list == keys:
                        self.send_image(addr_timestamp_dict, ip_addr, timestamp)
                        deletion_keys.append((ip_addr, timestamp))

        for ip_addr, timestamp in deletion_keys:
            del self.data[ip_addr][timestamp]

        # delete data for ip_addr if not in use any more
        deletion_keys = []
        for key in self.data.keys():
            if key not in [esp.addr for esp in esp_list]:
                deletion_keys.append(key)
        for key in deletion_keys:
            del self.data[key]
            logging.info("delete esp data at "+ str(key))

    def send_image(self, data, ip_addr, timestamp):
        logging.info("send image")
        byte_str = b''.join(data.values())
        msg = {}
        msg['data'] = base64.b64encode(byte_str).decode('ascii')
        msg['timestamp'] = timestamp
        msg['source_ipaddr'] = ip_addr
        msg_json = json.dumps(msg)
        return_value = self.mqtt_client.publish("server/images/data", msg_json, qos=2)
        if return_value[0] != 0:
            logging.error("error while sending")

    __call__ = collect
