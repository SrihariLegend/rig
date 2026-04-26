#include "event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <stdbool.h>

EventBus *event_bus_create(void) {
    EventBus *bus = calloc(1, sizeof(EventBus));
    if (!bus) return NULL;
    bus->sub_capacity = 16;
    bus->subs = calloc(bus->sub_capacity, sizeof(BusSubscription));
    bus->reply_capacity = 8;
    bus->replies = calloc(bus->reply_capacity, sizeof(BusReplyEntry));
    if (!bus->subs || !bus->replies) {
        free(bus->subs);
        free(bus->replies);
        free(bus);
        return NULL;
    }
    return bus;
}

void event_bus_free(EventBus *bus) {
    if (!bus) return;
    for (int i = 0; i < bus->sub_count; i++) free(bus->subs[i].pattern);
    for (int i = 0; i < bus->reply_count; i++) free(bus->replies[i].topic);
    free(bus->subs);
    free(bus->replies);
    free(bus);
}

static bool topic_matches(const char *pattern, const char *topic) {
    return fnmatch(pattern, topic, 0) == 0;
}

void event_bus_publish(EventBus *bus, const char *topic, const char *source, cJSON *data) {
    if (!bus || !topic) return;

    BusEvent event = {
        .topic = (char *)topic,
        .source = (char *)source,
        .data = data,
        .timestamp = (int64_t)time(NULL) * 1000,
    };

    for (int i = 0; i < bus->sub_count; i++) {
        if (topic_matches(bus->subs[i].pattern, topic)) {
            bus->subs[i].handler(&event, bus->subs[i].ctx);
        }
    }
}

void event_bus_subscribe(EventBus *bus, const char *pattern, BusHandler handler, void *ctx) {
    if (!bus || !pattern || !handler) return;

    if (bus->sub_count >= bus->sub_capacity) {
        int new_cap = bus->sub_capacity * 2;
        BusSubscription *new_subs = realloc(bus->subs, new_cap * sizeof(BusSubscription));
        if (!new_subs) return;
        bus->subs = new_subs;
        bus->sub_capacity = new_cap;
    }

    bus->subs[bus->sub_count++] = (BusSubscription){
        .pattern = strdup(pattern),
        .handler = handler,
        .ctx = ctx,
    };
}

cJSON *event_bus_request(EventBus *bus, const char *topic, cJSON *data, int timeout_ms) {
    if (!bus || !topic) return NULL;
    (void)timeout_ms;

    for (int i = 0; i < bus->reply_count; i++) {
        if (strcmp(bus->replies[i].topic, topic) == 0) {
            return bus->replies[i].handler(data, bus->replies[i].ctx);
        }
    }
    return NULL;
}

void event_bus_reply_handler(EventBus *bus, const char *topic, BusReplyHandler handler, void *ctx) {
    if (!bus || !topic || !handler) return;

    if (bus->reply_count >= bus->reply_capacity) {
        int new_cap = bus->reply_capacity * 2;
        BusReplyEntry *new_replies = realloc(bus->replies, new_cap * sizeof(BusReplyEntry));
        if (!new_replies) return;
        bus->replies = new_replies;
        bus->reply_capacity = new_cap;
    }

    bus->replies[bus->reply_count++] = (BusReplyEntry){
        .topic = strdup(topic),
        .handler = handler,
        .ctx = ctx,
    };
}

int event_bus_subscriber_count(EventBus *bus, const char *topic) {
    if (!bus || !topic) return 0;
    int count = 0;
    for (int i = 0; i < bus->sub_count; i++) {
        if (topic_matches(bus->subs[i].pattern, topic)) count++;
    }
    return count;
}
