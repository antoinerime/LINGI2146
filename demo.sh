# publish to a topic
mosquitto_pub -p 6000 -h localhost -t /sensor/temps -m 42

# subscribe a topic
mosquitto_sub -p 6000 -h localhost -t /sensor/temps

# run mosquitto
mosquitto -p 6000
