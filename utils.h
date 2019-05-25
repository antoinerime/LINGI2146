#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include "contiki.h"

#include "dev/sht11/sht11.h"


#define UINT8_MAX_VALUE 255
#define SIZE_ARRAY_CHILDREN 10
#define BROADCAST_INTERVAL 4
#define TIMEOUT_CHILD BROADCAST_INTERVAL*4
#define TIMEOUT_BUFFER 3
#define SIZE_BUF 5

#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

#define BROADCAST_CHANNEL 129
#define RUNICAST_CHANNEL 144

/* Topics definition */
#define TEMPERATURE 1
#define HUMIDITY 2

/* Options definition */
#define SEND_PERIODIC 1
#define SEND_IF_CHANGE 2

static struct runicast_conn runicast;
static struct broadcast_conn broadcast;

enum types {
  ADVERTISEMENT = 1,
  REQUEST = 2,
  DATA = 3,
  PARENT_DEAD = 4,
  OPTION = 5
};

/* Messages data strcutures */

typedef struct message {
  uint8_t type;
} message_t;

typedef struct advertisement {
  uint8_t type;
  uint8_t id;
} advertisement_t;

typedef struct request_parent {
  uint8_t type;
} request_parent_t;

typedef struct data {
  uint8_t type;
  linkaddr_t sensor_addr;
  uint8_t topic;
  int metric;
} data_t;

typedef struct parent_dead {
  uint8_t type;
} parent_dead_t;

typedef struct option {
  uint8_t type;
  uint8_t option;
} option_t;

/* Nodes data strcutures */

typedef struct parent {
  linkaddr_t addr;
  int16_t rssi;
  uint8_t id;
} parent_t;

typedef struct child {
  linkaddr_t addr;
  unsigned long last_seen;
} child_t;

/* Buffer data structure */

typedef struct data_buf {
  data_t *buf[SIZE_BUF];
  unsigned long timer;
} data_buf_t;


/* Utils functions */

static int get_temp() {
  return (unsigned) (-39.60 + 0.01 * sht11_temp());
}

static int get_hum() {
  int rh = sht11_humidity();
  return (unsigned) (-4 + 0.405*rh + 2.8e-6*(rh*rh));
}

static void send_option_to_children(uint8_t option, child_t **child_array) {
  option_t option_packet;
  option_packet.type = OPTION;
  option_packet.option = option;
  /* Flood option packet to all children */
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    if(child_array[i] != NULL) {
      packetbuf_clear();
      packetbuf_copyfrom(&option_packet, sizeof(option_t));
      runicast_send(&runicast, &child_array[i]->addr, MAX_RETRANSMISSIONS);
      packetbuf_clear();
    }
  }
}

static void send_request_parent(parent_t *parent, const linkaddr_t *new_parent_addr, uint8_t id) {
  parent->id = id;
  parent->addr.u8[0] = 0;
  parent->addr.u8[1] = 0;
  request_parent_t req;
  req.type = REQUEST;
  packetbuf_clear();
  packetbuf_copyfrom(&req, sizeof(request_parent_t));
  runicast_send(&runicast, new_parent_addr, MAX_RETRANSMISSIONS);
  packetbuf_clear();
}

static void send_broadcast_parent_dead(child_t **child_array) {
  printf("My parent is dead -> I send broadcast parent dead \n");
  parent_dead_t parent_dead;
  parent_dead.type = PARENT_DEAD;
  packetbuf_clear();
  packetbuf_copyfrom(&parent_dead, sizeof(data_t));
  broadcast_send(&broadcast);
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    child_array[i] = NULL;
  }
}

static void check_child_timeout(child_t **child_array) {
  unsigned long timer;
  timer = clock_seconds();
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    if(child_array[i] != NULL) {
      if(timer - child_array[i]->last_seen > TIMEOUT_CHILD) {
        printf("Remove child : %d.%d \n", child_array[i]->addr.u8[0], child_array[i]->addr.u8[1]);
        free(child_array[i]);
        child_array[i] = NULL;
      }
    }
  }
}

static void add_packet_to_buf(data_buf_t *data_buf, data_t *data_packet, linkaddr_t *to) {
  int i;
  for(i = 0; i < SIZE_BUF; i++) {
    if(data_buf->buf[i] == NULL) {
      data_t* pkt = (data_t *) malloc(sizeof(data_t));
      memcpy(pkt, data_packet, sizeof(data_t));
      data_buf->buf[i] = pkt;
      // data_t* pkt = (data_t *) data_buf->buf[i];
      // printf("Add type: %d, data: %d, data_buf: %p, i: %d\n",pkt->type, pkt->metric, data_buf->buf[i], i);
      data_buf->timer = clock_seconds();
      break;
    }
  }
  if(i == SIZE_BUF-1) {
    char buf[SIZE_BUF * sizeof(data_t)];
    for(i = 0; i < SIZE_BUF; i++) {
      if(data_buf->buf[i] != NULL) {
        memcpy(buf + i * sizeof(data_t), data_buf->buf[i], sizeof(data_t));
        free(data_buf->buf[i]);
        data_buf->buf[i] = NULL;
      }
    }
    int size = packetbuf_copyfrom(buf, sizeof(data_t) * SIZE_BUF);
    printf("Send buffer because full: size = %d \n", size);
    runicast_send(&runicast, to, MAX_RETRANSMISSIONS);
    data_buf->timer = clock_seconds();
  }
}

static void check_buffer_timeout(data_buf_t *data_buf, linkaddr_t *to) {
  unsigned long timer;
  timer = clock_seconds();
  if(timer - data_buf->timer > TIMEOUT_BUFFER) {
    int i;
    char buf[SIZE_BUF * sizeof(data_t)];
    // size_t size = 0;
    for(i = 0; i < SIZE_BUF; i++) {
      if(data_buf->buf[i] != NULL) {
        memcpy(buf + i * sizeof(data_t), data_buf->buf[i], sizeof(data_t));
        data_t* pkt = (data_t *) data_buf->buf[i];
        free(data_buf->buf[i]);
        data_buf->buf[i] = NULL;
      }
      else
        break;
    }
    if(i != 0) {
      int size = packetbuf_copyfrom(buf, i * sizeof(data_t));
      printf("Send buffer of size: %d because timeout \n", size);
      runicast_send(&runicast, to, MAX_RETRANSMISSIONS);
    }
    data_buf->timer = clock_seconds();
  }
}

static child_t *in_list_of_children(child_t **child_array ,const linkaddr_t *addr) {
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    if(child_array[i] != NULL && linkaddr_cmp(addr, &child_array[i]->addr)) {
      return child_array[i];
    }
  }
  return NULL;
}

static void add_child_to_list(child_t **child_array, const linkaddr_t *addr) {
  if(in_list_of_children(child_array, addr) == NULL) {
    child_t *new_child = malloc(sizeof(child_t));
    new_child->addr.u8[0] = addr->u8[0]; // TODO: use linkaddr_set
    new_child->addr.u8[1] = addr->u8[1];
    new_child->last_seen = clock_seconds();
    int i = 0;
    for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
      if(child_array[i] == NULL) {
        child_array[i] = new_child;
        break;
      }
    }
  }
}

static void print_child_list(child_t **child_array) {
  printf("My list of childs : \n");
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    if(child_array[i] != NULL) {
      printf("Addr child : %d.%d \n", child_array[i]->addr.u8[0], child_array[i]->addr.u8[1]);
    }
  }
}

static void send_data_temp(int temperature, data_buf_t *data_buf, linkaddr_t *parent_addr) {
  data_t data;
  data.type = DATA;
  data.sensor_addr = linkaddr_node_addr;
  data.topic = TEMPERATURE;
  data.metric = temperature;
  add_packet_to_buf(data_buf, &data, parent_addr);

}

static void send_data_hum(int humidity, data_buf_t *data_buf, linkaddr_t *parent_addr) {
  data_t data;
  data.type = DATA;
  data.sensor_addr = linkaddr_node_addr;
  data.topic = HUMIDITY;
  data.metric = humidity;
  add_packet_to_buf(data_buf, &data, parent_addr);
}



#endif
