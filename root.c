#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include "utils.h"
#include "mmem.h"
#include "linkaddr.h"

/*---------------------------------------------------------------------------*/
static child_t **child_array = NULL;
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
    add_child_to_list(child_array, from);
  }
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT: // discard advertisement because I'm the root
      break;
    case REQUEST:
      add_child_to_list(child_array, from); // In fact it is useless (if we use a channel for control plane then useful)
      break;
    case DATA:
      ;
      data_t *data_packet = (data_t *) message;
      printf("DATA: %d.%d, %d, %d\n", data_packet->sensor_addr.u8[0], data_packet->sensor_addr.u8[1], data_packet->topic, data_packet->metric); // Sends data to gateway
      break;
    default:
      printf("unicast_recv: Type not known \n");
  }
  //printf("runicast message received from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);
  print_child_list(child_array);
}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
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
  //printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}
static const struct broadcast_callbacks broadcast_callbacks = {broadcast_recv};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_network, ev, data)
{
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

  clock_init();
  while(1)
  {
    check_child_timeout(child_array);
    etimer_set(&et, CLOCK_SECOND * BROADCAST_INTERVAL + random_rand() % (CLOCK_SECOND * BROADCAST_INTERVAL));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    advertisement_t adv;
    adv.type = ADVERTISEMENT;
    adv.id = 0;
    packetbuf_clear();
    packetbuf_copyfrom(&adv, sizeof(advertisement_t));
    //printf("type : %d \n", ((int) ((advertisement_t *) packetbuf_dataptr())->type));
    broadcast_send(&broadcast);
    printf("Broadcast message sent : I can be your parent !\n");
  }

  PROCESS_END();
}
