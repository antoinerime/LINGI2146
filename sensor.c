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
static uint8_t option = 0;
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
      add_child_to_list(child_array, from);
    }
    else {
      send_broadcast_parent_dead(child_array); // node doesn't have a parent -> broadcast it
    }
  }
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT:
      break;
    case REQUEST:
      add_child_to_list(child_array, from); // In fact it is useless (if we use a channel for control plane then useful)
      break;
    case DATA:
      ;
      data_t *data_packet = (data_t *) message;
      packetbuf_clear();
      packetbuf_copyfrom(data_packet, sizeof(data_t));
      runicast_send(&runicast, &parent->addr, MAX_RETRANSMISSIONS);
      packetbuf_clear();
      //printf("forward data (from: %d.%d)\n", data_packet->sensor_addr.u8[0], data_packet->sensor_addr.u8[1]);
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
      /* Flood option packet to all children */
      int i = 0;
      for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
        if(child_array[i] != NULL) {
          packetbuf_clear();
          packetbuf_copyfrom(option_packet, sizeof(option_t));
          runicast_send(&runicast, &child_array[i]->addr, MAX_RETRANSMISSIONS);
          packetbuf_clear();
        }
      }
      break;
    default:
      printf("unicast_recv: Type not known \n");
  }
  //printf("runicast message received from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);
  //print_child_list();
}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  if(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0) { // We have a new parent
    parent->addr.u8[0] = to->u8[0];
    parent->addr.u8[1] = to->u8[1];
    printf("NEW PARENT, id: %d \n", parent->id);
  }
  //printf("runicast message sent to %d.%d. Parent : %d.%d.\n", to->u8[0], to->u8[1], parent->addr.u8[0], parent->addr.u8[1]);
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
      printf("broadcast_recv: Type not known \n");
  }
  //printf("broadcast message received from %d.%d: 'type = %d'\n", from->u8[0], from->u8[1], (int) message->type);
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

  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();

  broadcast_open(&broadcast, BROADCAST_CHANNEL, &broadcast_callbacks);
  runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
  clock_init();
  while(1)
  {
    check_child_timeout(child_array);
    etimer_set(&et, CLOCK_SECOND * BROADCAST_INTERVAL + random_rand() % (CLOCK_SECOND * BROADCAST_INTERVAL));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0)) { // If sensor has a parent, it advertises that it can becomes parent of other sensors
      advertisement_t adv;
      adv.type = ADVERTISEMENT;
      adv.id = (parent->id)+1;
      packetbuf_clear();
      packetbuf_copyfrom(&adv, sizeof(advertisement_t));
      broadcast_send(&broadcast);
      //printf("Broadcast message sent: I can be your parent! \n");

      /* Send data */
      data_t data;
      data.type = DATA;
      data.sensor_addr = linkaddr_node_addr;
      data.topic = TEMPERATURE;
      data.metric = 42;
      packetbuf_clear();
      packetbuf_copyfrom(&data, sizeof(data_t));
      runicast_send(&runicast, &parent->addr, MAX_RETRANSMISSIONS);
      packetbuf_clear();
      printf("Send data \n");
    }


  }

  PROCESS_END();
}
