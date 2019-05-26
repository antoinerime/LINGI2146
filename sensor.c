#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"


#include "utils.h"
#include "mmem.h"
#include "linkaddr.h"
#include "etimer.h"

/*---------------------------------------------------------------------------*/
static parent_t *parent = NULL;
static child_t **child_array = NULL;
static uint8_t option = SEND_PERIODIC;
static data_buf_t *data_buf;
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS(sensor_network, "sensor network");
AUTOSTART_PROCESSES(&sensor_network);
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  child_t *child = in_list_of_children(child_array, from);
  if(child != NULL) {
    child->last_seen = clock_seconds();
  }
  else {
    if(parent != NULL) {
      if(!linkaddr_cmp(&parent->addr, from)) {
        add_child_to_list(child_array, from);
      }
    }
    else {
      send_broadcast_parent_dead(child_array); // node doesn't have a parent -> broadcast it
    }
  }
  message_t *message = ((message_t *) packetbuf_dataptr());
  uint16_t nbr_data_packets = packetbuf_datalen() / sizeof(data_t);
  printf("DATA received, nbr packets = %d\n", (int) nbr_data_packets);
  switch((int) message->type) {
    case ADVERTISEMENT:
      break;
    case REQUEST:
      add_child_to_list(child_array, from);
      break;
    case DATA:
      ;
      uint16_t nbr_data_packets = packetbuf_datalen() / sizeof(data_t);
      printf("DATA received, nbr packets = %d\n", (int) nbr_data_packets);
      int i;
      for(i = 0; i < nbr_data_packets; i++) {
        add_packet_to_buf(data_buf, ((data_t *) message) + i, &parent->addr);
      }

      break;
    case OPTION:
      ;
      option_t *option_packet = (option_t *) message;
      switch((int) option_packet->option) {
        case SEND_PERIODIC:
          option = SEND_PERIODIC;
          break;
        case SEND_IF_CHANGE:
          option = SEND_IF_CHANGE;
          break;
      }
      printf("New option : %d \n", (int) option);
      /* Flood option packet to all children */
      send_option_to_children(option, child_array);
      break;
    default:
      printf("unicast_recv: Type not known %d\n", message->type);
  }

}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  if(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0) { // We have a new parent
    parent->addr.u8[0] = to->u8[0];
    parent->addr.u8[1] = to->u8[1];
    printf("NEW PARENT, id: %d \n", parent->id);
  }
}
static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  if(linkaddr_cmp(to, &parent->addr)) {
      parent = NULL; // parent is dead
      send_broadcast_parent_dead(child_array);
  }
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};

/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  child_t *child = in_list_of_children(child_array, from);
  if(child != NULL) {
    child->last_seen = clock_seconds();
  }
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT:
      ;
      advertisement_t *adv_message = (advertisement_t *) message;
      if(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0) // Don't have any parent
        send_request_parent(parent, from, adv_message->id);
      else if(adv_message->id < parent->id) // better number of hops
        send_request_parent(parent, from, adv_message->id);
      break;
    case REQUEST:
      break;
    case DATA:
      break;
    case PARENT_DEAD:
      if(linkaddr_cmp(from, &parent->addr)) {
        parent = NULL;
        send_broadcast_parent_dead(child_array);
      }
      break;
    default:
      printf("broadcast_recv: Type not known, type: %d \n", (int) message->type);
  }
}
static const struct broadcast_callbacks broadcast_callbacks = {broadcast_recv};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_network, ev, data)
{
  if(parent == NULL) {
    parent = malloc(sizeof(parent_t));
    parent->addr.u8[0] = 0;
    parent->addr.u8[1] = 0;
    parent->rssi = 0;
    parent->id = UINT8_MAX_VALUE;
  }

  if(child_array == NULL) {
    child_array = (child_t **) malloc(SIZE_ARRAY_CHILDREN * sizeof(child_t *));
    int i = 0;
    for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
      child_array[i] = NULL;
    }
  }

  if(data_buf == NULL) {
    data_buf = malloc(sizeof(data_buf_t));
    int i;
    for(i = 0; i < SIZE_BUF; i ++) {
      data_buf->buf[i] = NULL;
    }
    data_buf->timer = 0;
  }

  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();

  broadcast_open(&broadcast, BROADCAST_CHANNEL, &broadcast_callbacks);
  runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
  clock_init();
  int prev_temp = -1;
  int prev_hum = -1;

  while(1)
  {
    etimer_set(&et, CLOCK_SECOND * BROADCAST_INTERVAL + random_rand() % (CLOCK_SECOND * BROADCAST_INTERVAL));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0)) { // If sensor has a parent, it advertises that it can becomes parent of other sensors
      check_buffer_timeout(data_buf, &parent->addr); // check if the buffer timed out
      check_child_timeout(child_array); // Check if our children are still alive

      advertisement_t adv;
      adv.type = ADVERTISEMENT;
      adv.id = (parent->id)+1;
      packetbuf_copyfrom(&adv, sizeof(advertisement_t));
      broadcast_send(&broadcast);

      /* Send data */
      switch (option) {
        case SEND_PERIODIC:
          ;
          send_data_hum(get_hum(), data_buf, &parent->addr);
          send_data_temp(get_temp(), data_buf, &parent->addr);
          break;
        case SEND_IF_CHANGE:
          ;
          int temp = get_temp();
          int hum = get_hum();
          if (prev_temp != temp) {
            send_data_temp(temp, data_buf, &parent->addr);
            prev_temp = temp;
          }
          if (prev_hum != hum) {
            send_data_hum(hum, data_buf, &parent->addr);
            prev_hum = hum;
          }
          break;
      }


      print_child_list(child_array);
    }


  }

  PROCESS_END();
}
