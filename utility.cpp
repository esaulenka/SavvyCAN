#include "utility.h"

bool Utility::decimalMode = false;
QString Utility::timeFormat = "MMM-dd HH:mm:ss.zzz";
TimeStyle Utility::timeStyle = TS_MICROS;
QString Utility::fullyQualifiedNameSeperator = "::";


// Convert a FDCAN_data_length_code to number of bytes in a message
uint8_t Utility::dlc_code_to_bytes(int dlc_code)
{
    if (dlc_code<=8)
        return dlc_code;
    else{
        switch(dlc_code)
        {
        case 9:
            return 12;
        case 10:
            return 16;
        case 11:
            return 20;
        case 12:
            return 24;
        case 13:
            return 32;
        case 14:
            return 48;
        case 15:
            return 64;
        default:
            return 0;
        }
    }
}

uint8_t Utility::bytes_to_dlc_code(uint8_t bytes)
{
    if (bytes<=8)
        return bytes;
    else{
        switch(bytes)
        {
        case 12:
            return 9;
        case 16:
            return 10;
        case 20:
            return 11;
        case 24:
            return 12;
        case 32:
            return 13;
        case 48:
            return 14;
        case 64:
            return 15;
        default:
            return 0;
        }
    }
}
