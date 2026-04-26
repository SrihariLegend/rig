#ifndef PI_EVENT_BUS_H
#define PI_EVENT_BUS_H

#include "cjson/cJSON.h"
#include <stdint.h>

typedef struct {
    char *topic;
    char *source;
    cJSON *data;
    int64_t timestamp;
} BusEvent;

typedef void (*BusHandler)(BusEvent *event, void *ctx);
typedef cJSON *(*BusReplyHandler)(cJSON *request, void *ctx);

typedef struct {
    char *pattern;
    BusHandler handler;
    void *ctx;
} BusSubscription;

typedef struct {
    char *topic;
    BusReplyHandler handler;
    void *ctx;
} BusReplyEntry;

typedef struct {
    BusSubscription *subs;
    int sub_count;
    int sub_capacity;
    BusReplyEntry *replies;
    int reply_count;
    int reply_capacity;
} EventBus;

EventBus *event_bus_create(void);
void event_bus_free(EventBus *bus);

void event_bus_publish(EventBus *bus, const char *topic, const char *source, cJSON *data);
void event_bus_subscribe(EventBus *bus, const char *pattern, BusHandler handler, void *ctx);
cJSON *event_bus_request(EventBus *bus, const char *topic, cJSON *data, int timeout_ms);
void event_bus_reply_handler(EventBus *bus, const char *topic, BusReplyHandler handler, void *ctx);

int event_bus_subscriber_count(EventBus *bus, const char *topic);

#endif
