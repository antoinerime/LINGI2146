#!/usr/bin/env python3

import serial
import socket
import os
import re

HOST = '127.0.0.1'
PORT = 60002

def handle_line(line):
    regex = r"DATA: (?P<address>\d+.\d+), (?P<topic>\d+.\d+), (?P<metric>\d+.\d+)"
    match = re.match(r"DATA: (?P<address>\d+\.\d+), (?P<topic>\d+), (?P<metric>\d+)", line)
    if(match == None): return
    else:
        topic = match.group('topic')
        metric = match.group('metric')
        os.system('mosquitto_pub -h localhost -p 6000 -t {} -m {}'.format(topic, metric))

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
