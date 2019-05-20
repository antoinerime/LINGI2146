#!/usr/bin/env python3

import socket
import os
import re
import sys

from threading import Thread, Lock

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

def handle_cmd(s, cmd):
    if cmd == "periodical_data":
        s.send("periodical_data\n".encode())
    elif cmd == "on_change_data":
        s.send("on_change_data\n".encode())
    elif cmd == "":
        pass
    else:
        print("Command {} not known\n Try help for the list of commands", CMD)

def cli(socket):
    while True:
        try:
            cmd = sys.stdin.readline().strip('\n').strip(' ')
            if cmd == "help":
                print("Commands are: \n\t periodical_data, \n\t on_change_data")
            else:
                handle_cmd(socket, cmd)
        except Exception as e:
            print("Caught an exception: {}".format(e))

if __name__ == "__main__":
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        t = Thread(target=cli, name="cli", args=(s,), daemon=True)
        t.start()
        line = ""
        while True:
            data = (s.recv(1024)).decode("utf-8")
            if(data != "\n"):
                line += data
            else:
                line += data
                handle_line(line)
