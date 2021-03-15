///
/// Support for Touch Controller GSL1680
///
/// Information is quite scarce for the SiLead GSL1680 Capacitive Touch controller.
/// Further, this controller hosts a 32-bit micro, but is without flash memory, so
/// after power-up, the program must be installed into the chip. This is done through
/// the I2C interface. 
///
/// @caution To make it a bit more complicated, I cannot find any source
///     for this micro, only "bytestream", which one has to hope is defect free.
/// 
/// @caution To make it even more complicated, I do not have a display with this
///     controller in it, so this body of work is the collective merge of what I've
///     found and interpreted and interface converted to align with this library.
///
/// @caution It is probably clear that this represents a work in process, and is 
///     an attempt to make a working driver.
///
/// http://linux-sunxi.org/GSL1680 has some useful information, a bit of which
/// is replicated here in case that site changes/disappears.
///
/// To read from the chip, just write a single byte with the register number to 
/// start to read, and then do as many reads as registers to read. 
/// 
/// The known registers are: 
/// * 0x00-0x7F: these registers are used to load portions of the firmware.
/// * 0x80: contains the number of touches in the screen. 
/// ** If zero, the user isn't touching the screen; 
/// ** if one, only one finger is on the screen; 
/// ** if two, there are two fingers; and so on. 
/// * 0x84-0x87: contains the coordinates for the first touch. 
/// * 0x88-0x8B: contains the coordinates for the second touch. 
/// * 0x8C-0xAB: contains the coordinates for the third, fourth, and so on 
/// ** (up to five in some devices, up to ten in other), touches, 
///     in the same format than the previous ones (four bytes for each touch). 
/// * 0xE0: STATUS register 
/// * 0xE4, 0xBC-0xBF: some kind of control registers. Needed for uploading 
///     the firmware and soft resetting the chip 
///     (there's not more data available about them). 
/// * 0xF0: PAGE register. Contains the memory page number currently mapped 
///     in the 0x00-0x7F registers. 
///
/// Touch coordinates format
/// The four bytes of each group of coordinates contains the X and Y values, 
///     and also the finger. 
/// 
/// * The first two bytes contains, in little endian format, 
/// ** the X coordinate in the 12 lower bits. 
/// * The other two bytes contains, in little endian format, 
/// ** the Y coordinate in the 12 lower bits. 
/// ** The 4 upper bits in the Y coordinate contains the finger identifier.
///     
/// Example: 
/// Let's say that the user touches the screen with one finger. The register 0x80 
/// will contain 1, and registers 0x84 to 0x87 will contain the X and Y coordinates, 
/// and the finger identifier will be 1.
///  
/// Now the user, without removing the first finger, touches the screen with a second finger. 
/// The register 0x80 will contain 2. Registers 0x84 to 0x87 will contain the 
/// X and Y coordinates of the first touch and the finger identifier in them will be 1. 
/// Registers 0x88 to 0x8B will contain the X and Y coordinates of the second touch 
/// and the finger identifier in them will be 2.
///  
/// Now the user removes the first finger, keeping the second one. The register 0x80 
/// will contain 1. Registers 0x84 to 0x87 will contain the X and Y coordinates, 
/// but the finger identifier will be 2, because that's the finger that remains 
/// in the screen. 

#include "RA8875.h"
#include "RA8875_Touch_GSL1680_Firmware.h"

//#define DEBUG "RAGL"
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


#if 0
// Translate from GSL1680 Event Flag to Touch Code to API-match the
// alternate resistive touch screen driver common in the RA8875
// displays.
static const TouchCode_t GSL1680_EventFlagToTouchCode[4] = {
    touch,      // 00b Put Down
    release,    // 01b Put Up
    held,       // 10b Contact
    no_touch    // 11b Reserved
};
#endif

RetCode_t RA8875::GSL1680_Init() {
    RetCode_t r = noerror;
    uint8_t buf[5] = {0};   // addr + up to 4 Bytes data
    unsigned int source_line = 0;
    unsigned int source_len;
    const struct fw_data *ptr_fw;

    INFO("GSL1680_Init()");
    // Wake it
    m_wake->write(false);
    wait_ms(20);
    m_wake->write(true);
    wait_ms(20);
    
    // Clear reg
    buf[0] = 0xe0;
    buf[1] = 0x88;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    buf[0] = 0x80;
    buf[1] = 0x03;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    buf[0] = 0xe4;
    buf[1] = 0x04;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    buf[0] = 0xe0;
    buf[1] = 0x00;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    
    // Reset
    buf[0] = 0xe0;
    buf[1] = 0x88;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    buf[0] = 0xe4;
    buf[1] = 0x04;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(1);
    buf[0] = 0xbc;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    m_i2c->write(m_addr, (char *)buf, 5);
    wait_ms(1);
    
    // Load Firmware
    ptr_fw = GSLX680_FW;
    source_len = ARRAY_SIZE(GSLX680_FW);
    for (source_line = 0; source_line < source_len; source_line++) 
    {
        /* init page trans, set the page val */
        if (0xf0 == ptr_fw[source_line].offset)
        {
            buf[0] = 0xf0;
            buf[1] = (uint8_t)(ptr_fw[source_line].val & 0x000000ff);
            INFO("GSL1680 Firmware set page: %02X", buf[1]);
            m_i2c->write(m_addr, (char *)buf, 2);
        }
        else 
        {
            buf[0] = ptr_fw[source_line].offset;
            buf[1] = (uint8_t)(ptr_fw[source_line].val & 0x000000ff);
            buf[2] = (uint8_t)((ptr_fw[source_line].val & 0x0000ff00) >> 8);
            buf[3] = (uint8_t)((ptr_fw[source_line].val & 0x00ff0000) >> 16);
            buf[4] = (uint8_t)((ptr_fw[source_line].val & 0xff000000) >> 24);
            //INFO("GSL1680 Firmware write[%02X] = %08X", ptr_fw[source_line].offset, ptr_fw[source_line].val);
            m_i2c->write(m_addr, (char *)buf, 5);
        }
    }
    
    // Startup chip
    buf[0] = 0xe0;
    buf[1] = 0x00;
    m_i2c->write(m_addr, (char *)buf, 2);
    wait_ms(100);
    
    return r;
}

uint8_t RA8875::GSL1680_ReadRegU8(uint8_t reg, uint8_t * buf, int count) {
    uint8_t lbuf[1];
    
    if (buf == NULL) {
        buf = lbuf;
        count = 1;
    }
    m_i2c->write(m_addr, (char *)&reg, 1);
    m_i2c->read(m_addr, (char *)buf, count);
    return buf[0];
}


uint8_t RA8875::GSL1680_TouchPositions(void) {
    uint16_t fingerAndY, AndX;

    // [80] = # touch points
    // [81]       not used
    // [82]       not used
    // [83]       not used
    // [84] --+ Touch # 1 info
    // [85]   |
    // [86]   |
    // [87] --+
    // [88] --+ Touch # 2 info
    // [89]   |
    // [8A]   |
    // [8B] --+
    // ...    
    #define TD_SPACE (4 + 4 * GSL1680_TOUCH_POINTS)
    if (m_irq->read() == 0) {
        uint8_t touch_data[TD_SPACE];

        GSL1680_ReadRegU8(0x80, touch_data, TD_SPACE);
        numberOfTouchPoints = touch_data[0];
        gesture = FT5206_GEST_ID_NO_GESTURE;    // no gesture support

        INFO("GSL1680 Touch %d points", numberOfTouchPoints);
        int tNdx = GSL1680_TOUCH_POINTS - 1;
        int dNdx = TD_SPACE - 1;
        for ( ; tNdx >= 0; tNdx--, dNdx -= 4) {
            fingerAndY = (uint16_t)(touch_data[dNdx-0])<<8 | (uint16_t)touch_data[dNdx-1];
            AndX = (uint16_t)(touch_data[dNdx-2])<<8 | (uint16_t)touch_data[dNdx-3];
            touchInfo[tNdx].coordinates.y = fingerAndY & 0x0FFF;
            touchInfo[tNdx].coordinates.x = AndX & 0x0FFFF;
            touchInfo[tNdx].touchID = (fingerAndY >> 12);
            touchInfo[tNdx].touchCode = (numberOfTouchPoints > tNdx) ? touch : no_touch;
            //INFO("  Code %d, Finger %d, xy (%4d,%4d)",
            //    touchInfo[tNdx].touchCode, touchInfo[tNdx].touchID,
            //    touchInfo[tNdx].coordinates.x, touchInfo[tNdx].coordinates.y);
        }
    } else {
        numberOfTouchPoints = 0;
        ERR("GSL1680 Touch - else path, unexpected.");
    }
    return numberOfTouchPoints;
}
