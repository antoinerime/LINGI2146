#!/usr/bin/env python3

import serial
import socket
import os
import re

HOST = '127.0.0.1'
PORT = 60002

map_topics = {
    "1" : "/temperature/sensor",
    "2" : "/humidity/sensor",
    "3" : "/othermetric/sensor"
}

def handle_line(line):
    regex = r"DATA: (?P<address>\d+.\d+), (?P<topic>\d+.\d+), (?P<metric>\d+.\d+)"
    match = re.match(r"DATA: (?P<address>\d+\.\d+), (?P<topic>\d+), (?P<metric>\d+)", line)
    if(match == None): return
    else:
        topic = match.group('topic')
        string_topic = map_topics[str(topic)]
        metric = match.group('metric')
        os.system('mosquitto_pub -h localhost -p 6000 -t {} -m {}'.format(string_topic, metric))

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    line = ""
    while True:
        data = (s.recv(1024)).decode("utf-8")
        if(data != "\n"):
            line += data
        else:
            line += data
            handle_line(line)
