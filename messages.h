enum types {
  ADVERTISEMENT = 1,
  REQUEST = 2,
  DATA = 3
};

typedef struct broadcast_message {
  uint8_t type;
} broadcast_message_t;

typedef struct advertisement {
  uint8_t type;
  uint8_t id;
} advertisement_t;

typedef struct request_parent {
  uint8_t type;
} request_parent_t;

typedef struct data {
  uint8_t type;
  void *data;
} data_t;

typedef struct parent {
  linkaddr_t addr;
  int16_t rssi;
} parent_t;
