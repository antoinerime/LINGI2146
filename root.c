#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include "utils.h"
#include "mmem.h"
#include "linkaddr.h"
#include "dev/serial-line.h"


/*---------------------------------------------------------------------------*/
static child_t **child_array = NULL;
static uint8_t network_on = 2;
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS(sensor_network, "sensor network");
//AUTOSTART_PROCESSES(&sensor_network);
PROCESS(serial, "Serial line process");
AUTOSTART_PROCESSES(&serial, &sensor_network);
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  child_t *child = in_list_of_children(child_array, from);
  if(child != NULL) {
    child->last_seen = clock_seconds();
  }
  else {
    if(network_on) {
      add_child_to_list(child_array, from);
    }
    else {
      send_broadcast_parent_dead(child_array); // node doesn't have a parent -> broadcast it
    }
  }
  message_t *message = ((message_t *) packetbuf_dataptr());
  switch((int) message->type) {
    case ADVERTISEMENT: // discard advertisement because I'm the root
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
        data_t *data_packet = ((data_t *) message) + i;
        printf("DATA: %d.%d, %d, %d\n", data_packet->sensor_addr.u8[0], data_packet->sensor_addr.u8[1], data_packet->topic, data_packet->metric);
      }
      break;
    default:
      printf("unicast_recv: Type not known \n");
  }
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
    }
  }

  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();

  broadcast_open(&broadcast, BROADCAST_CHANNEL, &broadcast_callbacks);
  runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);

  clock_init();
  serial_line_init();
  while(1)
  {
    if (network_on) {
      check_child_timeout(child_array);
      etimer_set(&et, CLOCK_SECOND * BROADCAST_INTERVAL + random_rand() % (CLOCK_SECOND * BROADCAST_INTERVAL));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

      /* Advertises that it can be parent of other sensors */
      advertisement_t adv;
      adv.type = ADVERTISEMENT;
      adv.id = 0;
      packetbuf_clear();
      packetbuf_copyfrom(&adv, sizeof(advertisement_t));
      broadcast_send(&broadcast);
    }
  }

  PROCESS_END();
}

PROCESS_THREAD(serial, ev, data)
{
  /* Read socket if CLI gateway sent commands */
  PROCESS_BEGIN();

  for(;;) {
    PROCESS_YIELD();
    if(ev == serial_line_event_message) {
      if(strcmp((char *) data, "periodical_data") == 0) {
        printf("rcv option periodical \n");
        send_option_to_children(SEND_PERIODIC, child_array);
      }
      else if(strcmp((char *) data, "on_change_data") == 0) {
        printf("rcv option on change \n");
        send_option_to_children(SEND_IF_CHANGE, child_array);
      }
      else if(strcmp((char *) data, "start_nodes") == 0) {
        printf("rcv option to start nodes\n");
        network_on = 1;
      }
      else if(strcmp((char *) data, "stop_nodes") == 0) {
        printf("%s\n","rcv option to stop nodes");
        network_on = 0;
        send_broadcast_parent_dead(child_array);
      }
      printf("received line: %s\n", (char *)data);
    }
  }
  PROCESS_END();
}
