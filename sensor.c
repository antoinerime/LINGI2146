#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include "messages.h"
#include "mmem.h"
#include "linkaddr.h"
#include "sensor.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

#define BROADCAST_CHANNEL 129
#define RUNICAST_CHANNEL 144

/*---------------------------------------------------------------------------*/
//static linkaddr_t parent_addr;
/* Init parent address */
//parent_addr.u8[0] = 0;
//parent_addr.u8[1] = 0;

static parent_t *parent = NULL;
static child_t **child_array = NULL;
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
static void send_request_parent(const linkaddr_t *new_parent_addr, uint8_t id) {
  parent->id = id;
  parent->addr.u8[0] = 0;
  parent->addr.u8[1] = 0;
  request_parent_t req;
  req.type = REQUEST;
  packetbuf_copyfrom(&req, sizeof(request_parent_t));
  while(runicast_is_transmitting(&runicast)) {}
  runicast_send(&runicast, new_parent_addr, MAX_RETRANSMISSIONS);
}

static void print_child_list(void) {
  printf("My list of childs : \n");
  int i = 0;
  for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
    if(child_array[i] != NULL) {
      printf("Addr child : %d.%d \n", child_array[i]->addr.u8[0], child_array[i]->addr.u8[1]);
    }
  }
}

/*---------------------------------------------------------------------------*/



/*---------------------------------------------------------------------------*/
PROCESS(sensor_network, "sensor network");
AUTOSTART_PROCESSES(&sensor_network);
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT:
      break;
    case REQUEST:
      ;
      child_t *child = malloc(sizeof(child_t));
      child->addr.u8[0] = from->u8[0]; // TODO: use linkaddr_set
      child->addr.u8[1] = from->u8[1];
      int i = 0;
      for(i = 0; i < SIZE_ARRAY_CHILDREN; i++) {
        if(child_array[i] == NULL) {
          child_array[i] = child;
          break;
        }
      }
      break;
    case DATA:
      ;
      data_t *data_packet = (data_t *) message;
      packetbuf_copyfrom(data_packet, sizeof(data_t));
      while(runicast_is_transmitting(&runicast)) {}
      runicast_send(&runicast, &parent->addr, MAX_RETRANSMISSIONS);
      break;
    default:
      printf("unicast_recv: Type not known \n");
  }
  printf("runicast message received from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);
  print_child_list();
}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  if(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0) { // We have a new parent
    parent->addr.u8[0] = to->u8[0];
    parent->addr.u8[1] = to->u8[1];
    printf("NEW PARENT, id: %d \n", parent->id);
  }
  printf("runicast message sent to %d.%d. Parent : %d.%d.\n", to->u8[0], to->u8[1], parent->addr.u8[0], parent->addr.u8[1]);
}
static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};

/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT:
      ;
      advertisement_t *adv_message = (advertisement_t *) message;
      if(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0)
        send_request_parent(from, adv_message->id);
      else if(adv_message->id < parent->id) // TODO: Add condition on signal strength
        send_request_parent(from, adv_message->id);
      break;
    case REQUEST:
      break;
    case DATA:
      break;
    default:
      printf("broadcast_recv: Type not known \n");
  }
  printf("broadcast message received from %d.%d: 'type = %d'\n",
         from->u8[0], from->u8[1], (int) message->type);
}
static const struct broadcast_callbacks broadcast_callbacks = {broadcast_recv};
static struct broadcast_conn broadcast;
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
      if(child_array[i] != NULL) printf("WHAAAT\n");
    }
  }

  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();

  broadcast_open(&broadcast, BROADCAST_CHANNEL, &broadcast_callbacks);
  runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);

  while(1)
  {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!(parent->addr.u8[0] == 0 && parent->addr.u8[1] == 0)) {
      advertisement_t adv;
      adv.type = ADVERTISEMENT;
      adv.id = (parent->id)+1;
      packetbuf_clear();
      packetbuf_copyfrom(&adv, sizeof(advertisement_t));
      broadcast_send(&broadcast);
      printf("broadcast message sent: I can be your parent! \n");

      data_t data;
      data.type = DATA;
      data.sensor_addr = linkaddr_node_addr;
      data.metric = 42;

      packetbuf_clear();
      packetbuf_copyfrom(&data, sizeof(data_t));
      while(runicast_is_transmitting(&runicast)) {}
      runicast_send(&runicast, &parent->addr, MAX_RETRANSMISSIONS);
    }


  }

  PROCESS_END();
}
