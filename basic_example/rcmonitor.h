#ifndef rcmonitor_h
#define rcmonitor_h

#define MONITORS_MAX 255
#define MONITOR_NAME_MAX 32
#define MONITOR_DEF_MAX 64

static const int32_t INVALID_VALUE = 0x7fffffff;

char monitorNames[MONITORS_MAX][MONITOR_NAME_MAX+1];
float monitorMultipliers[MONITORS_MAX];
int32_t monitorValues[MONITORS_MAX];
uint8_t activeMonitors = 0;

struct strmon {
    const char nam[MONITOR_NAME_MAX];
    const char def[MONITOR_DEF_MAX];
    float mul;
};

void rcmonitorstart(void);
boolean rcmonitor(void);

// Number of monitors that were configured successfully.
static inline uint8_t rcmonitorCount(void) {
    return activeMonitors;
}

// A monitor keeps INVALID_VALUE until the app has sent a value for it, which
// can take a lap or more for things like lap times.
static inline boolean rcmonitorHasValue(uint8_t monitorId) {
    return monitorId < activeMonitors && monitorValues[monitorId] != INVALID_VALUE;
}

// Raw integer scaled back to engineering units. Only meaningful when
// rcmonitorHasValue() is true for the same id.
static inline float rcmonitorValue(uint8_t monitorId) {
    return (float) monitorValues[monitorId] * monitorMultipliers[monitorId];
}

#endif
