

#include "RA8875.h"

//#define DEBUG "RAFT" // RA8875 FT5206
// ...
// INFO("Stuff to show %d", var); // new-line is automatically appended
//
#if (defined(DEBUG) && !defined(TARGET_LPC11U24))
#define INFO(x, ...) std::printf("[INF %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
#define WARN(x, ...) std::printf("[WRN %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
#define ERR(x, ...)  std::printf("[ERR %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
static void HexDump(const char * title, const uint8_t * p, int count)
{
    int i;
    char buf[100] = "0000: ";

    if (*title)
        INFO("%s", title);
    for (i=0; i<count; ) {
        sprintf(buf + strlen(buf), "%02X ", *(p+i));
        if ((++i & 0x0F) == 0x00) {
            INFO("%s", buf);
            if (i < count)
                sprintf(buf, "%04X: ", i);
            else
                buf[0] = '\0';
        }
    }
    if (strlen(buf))
        INFO("%s", buf);
}
#else
#define INFO(x, ...)
#define WARN(x, ...)
#define ERR(x, ...)
#define HexDump(a, b, c)
#endif



// Translate from FT5206 Event Flag to Touch Code to API-match the
// alternate resistive touch screen driver common in the RA8875
// displays.
static const TouchCode_t FT5206_EventFlagToTouchCode[4] = {
    touch,      // 00b Put Down
    release,    // 01b Put Up
    held,       // 10b Contact
    no_touch    // 11b Reserved
};


RetCode_t RA8875::FT5206_Init() {
    char data[2] = {FT5206_DEVICE_MODE, 0};

    m_i2c->write(m_addr, data, 2);
    return noerror;
}

uint8_t RA8875::FT5206_TouchPositions(void) {
    uint8_t valXH;
    uint8_t valYH;

    //INFO("FT5206_TouchPositions()");
    numberOfTouchPoints = FT5206_ReadRegU8(FT5206_TD_STATUS) & 0xF;
    //INFO("  numOfTouchPoints %d", numberOfTouchPoints);
    gesture = FT5206_ReadRegU8(FT5206_GEST_ID);
    //INFO("  gesture %d", gesture);
    
    valXH  = FT5206_ReadRegU8(FT5206_TOUCH5_XH);
    valYH  = FT5206_ReadRegU8(FT5206_TOUCH5_YH);
    touchInfo[4].touchCode = FT5206_EventFlagToTouchCode[valXH >> 6];
    touchInfo[4].touchID   = (valYH >> 4);
    touchInfo[4].coordinates.x = (valXH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH5_XL);
    touchInfo[4].coordinates.y = (valYH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH5_YL);
    
    valXH  = FT5206_ReadRegU8(FT5206_TOUCH4_XH);
    valYH  = FT5206_ReadRegU8(FT5206_TOUCH4_YH);
    touchInfo[3].touchCode = FT5206_EventFlagToTouchCode[valXH >> 6];
    touchInfo[3].touchID   = (valYH >> 4);
    touchInfo[3].coordinates.x = (valXH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH4_XL);
    touchInfo[3].coordinates.y = (valYH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH4_YL);
    
    valXH  = FT5206_ReadRegU8(FT5206_TOUCH3_XH);
    valYH  = FT5206_ReadRegU8(FT5206_TOUCH3_YH);
    touchInfo[2].touchCode = FT5206_EventFlagToTouchCode[valXH >> 6];
    touchInfo[2].touchID   = (valYH >> 4);
    touchInfo[2].coordinates.x = (valXH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH3_XL);
    touchInfo[2].coordinates.y = (valYH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH3_YL);
    
    valXH  = FT5206_ReadRegU8(FT5206_TOUCH2_XH);
    valYH  = FT5206_ReadRegU8(FT5206_TOUCH2_YH);
    touchInfo[1].touchCode = FT5206_EventFlagToTouchCode[valXH >> 6];
    touchInfo[1].touchID   = (valYH >> 4);
    touchInfo[1].coordinates.x = (valXH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH2_XL);
    touchInfo[1].coordinates.y = (valYH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH2_YL);
    
    valXH  = FT5206_ReadRegU8(FT5206_TOUCH1_XH);
    valYH  = FT5206_ReadRegU8(FT5206_TOUCH1_YH);
    touchInfo[0].touchCode = FT5206_EventFlagToTouchCode[valXH >> 6];
    touchInfo[0].touchID   = (valYH >> 4);
    touchInfo[0].coordinates.x = (valXH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH1_XL);
    touchInfo[0].coordinates.y = (valYH & 0x0f)*256 + FT5206_ReadRegU8(FT5206_TOUCH1_YL);

    return numberOfTouchPoints;
}
