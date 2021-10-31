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

#endif
