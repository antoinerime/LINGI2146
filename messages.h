#define UINT8_MAX_VALUE 255
#define SIZE_ARRAY_CHILDREN 10
#define BROADCAST_INTERVAL 4
#define TIMEOUT_CHILD BROADCAST_INTERVAL*4

enum types {
  ADVERTISEMENT = 1,
  REQUEST = 2,
  DATA = 3,
  PARENT_DEAD = 4
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
  int metric;
} data_t;

typedef struct parent_dead {
  uint8_t type;
} parent_dead_t;

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
