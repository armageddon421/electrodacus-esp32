#ifndef SBMS_DATA_H
#define SBMS_DATA_H

#include <WString.h>

class SbmsData {

public:
    SbmsData(String data);

    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t stateOfChargePercent;
    uint16_t cellVoltageMV[8];
    int16_t temperatureInternalTenthC;
    int16_t temperatureExternalTenthC;
    int32_t batteryCurrentMA;
    uint32_t pv1CurrentMA;
    uint32_t pv2CurrentMA;
    uint32_t extLoadCurrentMA;
    uint32_t ad2;
    uint32_t ad3;
    uint32_t ad4;
    uint16_t heat1;
    uint16_t heat2;
    uint16_t flags;

    enum FlagBit {
        OV = 0,
        OVLK = 1,
        UV = 2,
        UVLK = 3,
        IOT = 4,
        COC = 5,
        DOC = 6,
        DSC = 7,
        CELF = 8,
        OPEN = 9,
        LVC = 10,
        ECCF = 11,
        CFET = 12,
        EOC = 13,
        DFET = 14
    };

    bool getFlag(FlagBit bit) const;


protected:

    //decompresses the specified value, moves offset along by the given size
    uint32_t decompress(const char *data, uint16_t &offset, uint8_t size);


};

#endif