
#include "sbmsData.hpp"

#include <cmath>

SbmsData::SbmsData(String dataString)
{
    const char *data = dataString.c_str();

    uint16_t i = 1; //ignore quotation mark
    year = decompress(data, i, 1);
    month = decompress(data, i, 1);
    day = decompress(data, i, 1);
    hour = decompress(data, i, 1);
    minute = decompress(data, i, 1);
    second = decompress(data, i, 1);
    stateOfChargePercent = decompress(data, i, 2);

    for(uint8_t x=0; x<8; x++)
    {
        cellVoltageMV[x] = decompress(data, i, 2);
    }

    temperatureInternalTenthC = decompress(data, i, 2) - 450;
    temperatureExternalTenthC = decompress(data, i, 2) - 450;
    
    int8_t sign = data[i++] == '-'?-1:1;
    batteryCurrentMA = decompress(data, i, 3) * sign;
    pv1CurrentMA = decompress(data, i, 3);
    pv2CurrentMA = decompress(data, i, 3);
    extLoadCurrentMA = decompress(data, i, 3);
    ad2 = decompress(data, i, 3);
    ad3 = decompress(data, i, 3);

    //skip ht1 and ht2
    i += 6;

    flags = decompress(data, i, 2);
}

uint32_t SbmsData::decompress(const char *data, uint16_t &offset, uint8_t size)
{
    uint32_t xx=0;
    uint32_t expF = 1;
    for (uint8_t z=0; z<size; z++)
    {
        xx = xx + ((data[(offset+size-1)-z]-35)*expF);
        expF *= 91; //increase exponent for next character
    }
    offset += size;
    return xx;
}