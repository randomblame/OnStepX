// -----------------------------------------------------------------------------------------------------------------
// NTP time code
// from here: https://github.com/PaulStoffregen/Time
#pragma once

#include "../../Common.h"

#if defined(TIME_LOCATION_SOURCE) && TIME_LOCATION_SOURCE == NTP

#include <EthernetUdp.h>
#include "../calendars/Calendars.h"

class TimeLocationSource {
  public:
    // initialize (also enables the RTC PPS if available)
    bool init();

    // restart UDP server
    void restart();

    // true if date/time ready
    inline bool isReady() { return ready; }

    // set the RTC's time
    void set(JulianDate ut1);
    void set(int year, int month, int day, int hour, int minute, int second);

    // get the RTC's time
    void get(JulianDate &ut1);

    // update from NTP
    void poll();

  private:
    // send an NTP request to the time server at the given address
    void sendNTPpacket(IPAddress &address);

    // NTP time is in the first 48 bytes of message
    static const int NTP_PACKET_SIZE = 48;

    // buffer to hold incoming & outgoing packets
    uint8_t packetBuffer[NTP_PACKET_SIZE];

    unsigned long startTime = 0;
    bool ready = false;
    bool active = false;
    uint8_t handle = 0;
    
};

extern TimeLocationSource tls;
#endif
