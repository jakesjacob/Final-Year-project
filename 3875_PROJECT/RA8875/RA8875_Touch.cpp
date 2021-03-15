/// This file contains the RA8875 Touch panel methods.
///
/// It combines both resistive and capacitive touch methods, and tries
/// to make them nearly transparent alternates for each other.
///
#include "RA8875.h"
#include "RA8875_Touch_FT5206.h"
#include "RA8875_Touch_GSL1680.h"

#define NOTOUCH_TIMEOUT_uS 100000
#define TOUCH_TICKER_uS      1000

//#define DEBUG "TUCH"
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


RetCode_t RA8875::TouchPanelInit(void)
{
    RetCode_t r = noerror;
    
    panelTouched = false;
    if (useTouchPanel == TP_GSL1680) {
        INFO("TouchPanelInit: TP_GSL1680");
        r = GSL1680_Init();
    } else if (useTouchPanel == TP_FT5206) {
        // Set to normal mode
        INFO("TouchPanelInit: TP_FT5206");
        r = FT5206_Init();
    } else {
        INFO("TouchPanelInit: TP_RES");
        //TPCR0: Set enable bit, default sample time, wakeup, and ADC clock
        WriteCommand(TPCR0, TP_ENABLE | TP_ADC_SAMPLE_DEFAULT_CLKS | TP_ADC_CLKDIV_DEFAULT);
        // TPCR1: Set auto/manual, Ref voltage, debounce, manual mode params
        WriteCommand(TPCR1, TP_MODE_DEFAULT | TP_DEBOUNCE_DEFAULT);
        WriteCommand(INTC1, ReadCommand(INTC1) | RA8875_INT_TP);        // reg INTC1: Enable Touch Panel Interrupts (D2 = 1)
        WriteCommand(INTC2, RA8875_INT_TP);                            // reg INTC2: Clear any TP interrupt flag
        touchSample = 0;
        touchState = no_cal;
        //touchTicker.attach_us(callback(this, &RA8875::_TouchTicker), TOUCH_TICKER_uS);
        #if (MBED_MAJOR_VERSION >= 5) || (MBED_LIBRARY_VERSION > 122)   // Is this the right version?
        touchTicker.attach_us(callback(this, &RA8875::_TouchTicker), TOUCH_TICKER_uS);
        #else
        touchTicker.attach_us(this, &RA8875::_TouchTicker, TOUCH_TICKER_uS);
        #endif
        
        timeSinceTouch.start();
        timeSinceTouch.reset();
        r = _internal_ts_cal();
    }
    return r;
}


RetCode_t RA8875::TouchPanelInit(uint8_t bTpEnable, uint8_t bTpAutoManual, uint8_t bTpDebounce, uint8_t bTpManualMode, uint8_t bTpAdcClkDiv, uint8_t bTpAdcSampleTime)
{
    if (useTouchPanel == TP_GSL1680) {
        INFO("TouchPanelInit: TP_GSL1680");
        /// @todo Added support for TP_GSL1680
    } else if (useTouchPanel == TP_FT5206) {
        INFO("TouchPanelInit: TP_FT5206");
        TouchPanelInit();
    } else {
        INFO("TouchPanelInit: TP_RES");
        // Parameter bounds check
        if( \
                !(bTpEnable == TP_ENABLE || bTpEnable == TP_ENABLE) || \
                !(bTpAutoManual == TP_MODE_AUTO || bTpAutoManual == TP_MODE_MANUAL) || \
                !(bTpDebounce == TP_DEBOUNCE_OFF || bTpDebounce == TP_DEBOUNCE_ON) || \
                !(bTpManualMode <= TP_MANUAL_LATCH_Y) || \
                !(bTpAdcClkDiv <= TP_ADC_CLKDIV_128) || \
                !(bTpAdcSampleTime <= TP_ADC_SAMPLE_65536_CLKS) \
          ) return bad_parameter;
        // Construct the config byte for TPCR0 and write them
        WriteCommand(TPCR0, bTpEnable | bTpAdcClkDiv | bTpAdcSampleTime);    // Note: Wakeup is never enabled
        // Construct the config byte for TPCR1 and write them
        WriteCommand(TPCR1, bTpManualMode | bTpDebounce | bTpManualMode);    // Note: Always uses internal Vref.
        // Set up the interrupt flag and enable bits
        WriteCommand(INTC1, ReadCommand(INTC1) | RA8875_INT_TP);        // reg INTC1: Enable Touch Panel Interrupts (D2 = 1)
        WriteCommand(INTC2, RA8875_INT_TP);                            // reg INTC2: Clear any TP interrupt flag
        touchSample = 0;
        touchState = no_cal;
        if (bTpEnable == TP_ENABLE) {
            //touchTicker.attach_us(callback(this, &RA8875::_TouchTicker), TOUCH_TICKER_uS);
            #if (MBED_MAJOR_VERSION >= 5) || (MBED_LIBRARY_VERSION > 122)   // Is this the right version?
            touchTicker.attach_us(callback(this, &RA8875::_TouchTicker), TOUCH_TICKER_uS);
            #else
            touchTicker.attach_us(this, &RA8875::_TouchTicker, TOUCH_TICKER_uS);
            #endif

            timeSinceTouch.start();
            timeSinceTouch.reset();
        } else {
            touchTicker.detach();
            timeSinceTouch.stop();
        }
        _internal_ts_cal();
    }
    return noerror;
}


int RA8875::TouchChannels(void)
{
    if (useTouchPanel == TP_GSL1680) {
        return GSL1680_TOUCH_POINTS;  // based on TP_GSL1680 firmware
    } else if (useTouchPanel == TP_FT5206) {
        return 5;   // based on the FT5206 hardware
    } else if (useTouchPanel == TP_RES) {
        return 1;   // based on the RA8875 resistive touch driver
    } else {
        return 0;   // it isn't enabled, so there are none.
    }
}


// +----------------------------------------------------+
// |                                                    |
// |  1                                                 |
// |                                                    |
// |                                                    |
// |                                               2    |
// |                                                    |
// |                                                    |
// |                         3                          |
// |                                                    |
// +----------------------------------------------------+

RetCode_t RA8875::TouchPanelCalibrate(tpMatrix_t * matrix)
{
    return TouchPanelCalibrate(NULL, matrix);
}

RetCode_t RA8875::TouchPanelCalibrate(const char * msg, tpMatrix_t * matrix, int maxwait_s)
{
    point_t pTest[3];
    point_t pSample[3];
    int x,y;
    Timer timeout;  // timeout guards for not-installed, stuck, user not present...

    timeout.start();
    while (TouchPanelA2DFiltered(&x, &y) && timeout.read() < maxwait_s) {
        wait_ms(20);
        if (idle_callback) {
            if (external_abort == (*idle_callback)(touchcal_wait, 0)) {
                return external_abort;
            }
        }
    }
    cls();
    if (msg) {                  // User defines the message
        if (*msg) puts(msg);    // If any
    } else {                    // Default message
        puts("Touch '+' to calibrate the touch panel");
    }
    SetTextCursor(0,height()/2);
    pTest[0].x = 50;
    pTest[0].y = 50;
    pTest[1].x = width() - 50;
    pTest[1].y = height()/2;
    pTest[2].x = width()/2;
    pTest[2].y = height() - 50;

    for (int i=0; i<3; i++) {
        foreground(Blue);
        printf(" (%3d,%3d) => ", pTest[i].x, pTest[i].y);
        fillcircle(pTest[i].x,pTest[i].y, 20, White);
        line(pTest[i].x-10, pTest[i].y, pTest[i].x+10, pTest[i].y, Blue);
        line(pTest[i].x, pTest[i].y-10, pTest[i].x, pTest[i].y+10, Blue);
        while (!TouchPanelA2DFiltered(&x, &y) && timeout.read() < maxwait_s) {
            wait_ms(20);
            if (idle_callback) {
                if (external_abort == (*idle_callback)(touchcal_wait, 0)) {
                    return external_abort;
                }
            }
        }
        pSample[i].x = x;
        pSample[i].y = y;
        fillcircle(pTest[i].x,pTest[i].y, 20, Black);   // Erase the target
        //line(pTest[i].x-10, pTest[i].y, pTest[i].x+10, pTest[i].y, Black);
        //line(pTest[i].x, pTest[i].y-10, pTest[i].x, pTest[i].y+10, Black);
        foreground(Blue);
        printf(" (%4d,%4d)\r\n", x,y);
        while (TouchPanelA2DFiltered(&x, &y) && timeout.read() < maxwait_s) {
            wait_ms(20);
            if (idle_callback) {
                if (external_abort == (*idle_callback)(touchcal_wait, 0)) {
                    return external_abort;
                }
            }
        }
        for (int t=0; t<100; t++) {
            wait_ms(20);
            if (idle_callback) {
                if (external_abort == (*idle_callback)(touchcal_wait, 0)) {
                    return external_abort;
                }
            }
        }
    }
    if (timeout.read() >= maxwait_s)
        return touch_cal_timeout;
    else
        return TouchPanelComputeCalibration(pTest, pSample, matrix);
}


/**********************************************************************
 *
 *     Function: TouchPanelReadable()
 *
 *  Description: Given a valid set of calibration factors and a point
 *                value reported by the touch screen, this function
 *                calculates and returns the true (or closest to true)
 *                display point below the spot where the touch screen
 *                was touched.
 *
 *
 *
 *  Argument(s): displayPtr (output) - Pointer to the calculated
 *                                      (true) display point.
 *               screenPtr (input) - Pointer to the reported touch
 *                                    screen point.
 *               matrixPtr (input) - Pointer to calibration factors
 *                                    matrix previously calculated
 *                                    from a call to
 *                                    setCalibrationMatrix()
 *
 *
 *  The function simply solves for Xd and Yd by implementing the
 *   computations required by the translation matrix.
 *
 *                                              /-     -\
 *              /-    -\     /-            -\   |       |
 *              |      |     |              |   |   Xs  |
 *              |  Xd  |     | A    B    C  |   |       |
 *              |      |  =  |              | * |   Ys  |
 *              |  Yd  |     | D    E    F  |   |       |
 *              |      |     |              |   |   1   |
 *              \-    -/     \-            -/   |       |
 *                                              \-     -/
 *
 *  It must be kept brief to avoid consuming CPU cycles.
 *
 *       Return: OK - the display point was correctly calculated
 *                     and its value is in the output argument.
 *               NOT_OK - an error was detected and the function
 *                         failed to return a valid point.
 *
 *                 NOTE!    NOTE!    NOTE!
 *
 *  setCalibrationMatrix() and getDisplayPoint() will do fine
 *  for you as they are, provided that your digitizer
 *  resolution does not exceed 10 bits (1024 values).  Higher
 *  resolutions may cause the integer operations to overflow
 *  and return incorrect values.  If you wish to use these
 *  functions with digitizer resolutions of 12 bits (4096
 *  values) you will either have to a) use 64-bit signed
 *  integer variables and math, or b) judiciously modify the
 *  operations to scale results by a factor of 2 or even 4.
 *
 */
TouchCode_t RA8875::TouchPanelReadable(point_t * TouchPoint)
{
    TouchCode_t ts = no_touch;

    if (useTouchPanel == TP_FT5206) {
        ;
    } else if (useTouchPanel == TP_GSL1680) {
        ;
    } else if (useTouchPanel == TP_RES) {
        int a2dX = 0;
        int a2dY = 0;
        
        touchInfo[0].touchID = 0;
        ts = TouchPanelA2DFiltered(&a2dX, &a2dY);
        if (ts != no_touch) {
            panelTouched = true;
            numberOfTouchPoints = 1;

            if (tpMatrix.Divider != 0) {
                /* Operation order is important since we are doing integer */
                /*  math. Make sure you add all terms together before      */
                /*  dividing, so that the remainder is not rounded off     */
                /*  prematurely.                                           */
                touchInfo[0].coordinates.x = ( (tpMatrix.An * a2dX) +
                                  (tpMatrix.Bn * a2dY) + tpMatrix.Cn
                                ) / tpMatrix.Divider ;
                touchInfo[0].coordinates.y = ( (tpMatrix.Dn * a2dX) +
                                  (tpMatrix.En * a2dY) + tpMatrix.Fn
                                ) / tpMatrix.Divider ;
            } else {
                ts = no_cal;
            }
        } else {
            numberOfTouchPoints = 0;
        }
        touchInfo[0].touchCode = ts;
    }
    // For Resistive touch, panelTouched is computed above.
    // For Cap Sense, panelTouched is set in another process
    if (panelTouched == true) {
        panelTouched = false;
        if (TouchPoint) {
            *TouchPoint = touchInfo[0].coordinates;
            ts = touchInfo[0].touchCode;
            INFO("Touch[0] %2d (%4d,%4d)", touchInfo[0].touchCode,
                touchInfo[0].coordinates.x, touchInfo[0].coordinates.y);
        } else {
            ts = touch;
        }
    }
    return ts;
}

uint8_t RA8875::TouchID(uint8_t channel) 
{
    if (channel >= TouchChannels())
        channel = 0;
    return touchInfo[channel].touchID; 
}

TouchCode_t RA8875::TouchCode(uint8_t channel) 
{ 
    if (channel >= TouchChannels())
        channel = 0;
    return touchInfo[channel].touchCode; 
}

point_t RA8875::TouchCoordinates(uint8_t channel)
{
    if (channel >= TouchChannels())
        channel = 0;
    return touchInfo[channel].coordinates; 
}

TouchCode_t RA8875::TouchPanelGet(point_t * TouchPoint)
{
    TouchCode_t t = no_touch;

    while (true) {
        t = TouchPanelReadable(TouchPoint);
        if (t != no_touch)
            break;
        if (idle_callback) {
            if (external_abort == (*idle_callback)(touch_wait, 0)) {
                return no_touch;
            }
        }
    }
    return t;
}

// Below here are primarily "helper" functions. While many are accessible
// to the user code, they usually don't need to be called.

RetCode_t RA8875::TouchPanelSetMatrix(tpMatrix_t * matrixPtr)
{
    if (matrixPtr == NULL || matrixPtr->Divider == 0)
        return bad_parameter;
    memcpy(&tpMatrix, matrixPtr, sizeof(tpMatrix_t));
    touchState = no_touch;
    return noerror;
}

const tpMatrix_t * RA8875::TouchPanelGetMatrix()
{
    return &tpMatrix;
}


static void InsertionSort(int * buf, int bufsize)
{
    int i, j;
    int temp;

    for(i = 1; i < bufsize; i++) {
        temp = buf[i];
        j = i;
        while( j && (buf[j-1] > temp) ) {
            buf[j] = buf[j-1];
            j = j-1;
        }
        buf[j] = temp;
    } // End of sort
}


void RA8875::_TouchTicker(void)
{
    INFO("_TouchTicker()");
    if (timeSinceTouch.read_us() > NOTOUCH_TIMEOUT_uS) {
        touchSample = 0;
        if (touchState == held)
            touchState = release;
        else
            touchState = no_touch;
        timeSinceTouch.reset();
    }
}

TouchCode_t RA8875::TouchPanelA2DRaw(int *x, int *y)
{
    INFO("A2Raw");
    if( (ReadCommand(INTC2) & RA8875_INT_TP) ) {        // Test for TP Interrupt pending in register INTC2
        INFO("Int pending");
        timeSinceTouch.reset();
        *y = ReadCommand(TPYH) << 2 | ( (ReadCommand(TPXYL) & 0xC) >> 2 );   // D[9:2] from reg TPYH, D[1:0] from reg TPXYL[3:2]
        *x = ReadCommand(TPXH) << 2 | ( (ReadCommand(TPXYL) & 0x3)      );   // D[9:2] from reg TPXH, D[1:0] from reg TPXYL[1:0]
        INFO("(x,y) = (%d,%d)", x, y);
        WriteCommand(INTC2, RA8875_INT_TP);            // reg INTC2: Clear that TP interrupt flag
        touchState = touch;
    } else {
        INFO("no touch");
        touchState = no_touch;
    }
    return touchState;
}

TouchCode_t RA8875::TouchPanelA2DFiltered(int *x, int *y)
{
    static int xbuf[TPBUFSIZE], ybuf[TPBUFSIZE];
    static int lastX, lastY;
    int i, j;
    TouchCode_t ret = touchState;

    if( (ReadCommand(INTC2) & RA8875_INT_TP) ) {        // Test for TP Interrupt pending in register INTC2
        timeSinceTouch.reset();
        // Get the next data samples
        ybuf[touchSample] =  ReadCommand(TPYH) << 2 | ( (ReadCommand(TPXYL) & 0xC) >> 2 );   // D[9:2] from reg TPYH, D[1:0] from reg TPXYL[3:2]
        xbuf[touchSample] =  ReadCommand(TPXH) << 2 | ( (ReadCommand(TPXYL) & 0x3)      );   // D[9:2] from reg TPXH, D[1:0] from reg TPXYL[1:0]
        // Check for a complete set
        if(++touchSample == TPBUFSIZE) {
            // Buffers are full, so process them using Finn's method described in Analog Dialogue No. 44, Feb 2010
            // This requires sorting the samples in order of size, then discarding the top 25% and
            //   bottom 25% as noise spikes. Finally, the middle 50% of the values are averaged to
            //   reduce Gaussian noise.
#if 1
            InsertionSort(ybuf, TPBUFSIZE);
            InsertionSort(xbuf, TPBUFSIZE);
#else
            // Sort the Y buffer using an Insertion Sort
            for(i = 1; i <= TPBUFSIZE; i++) {
                temp = ybuf[i];
                j = i;
                while( j && (ybuf[j-1] > temp) ) {
                    ybuf[j] = ybuf[j-1];
                    j = j-1;
                }
                ybuf[j] = temp;
            } // End of Y sort
            // Sort the X buffer the same way
            for(i = 1; i <= TPBUFSIZE; i++) {
                temp = xbuf[i];
                j = i;
                while( j && (xbuf[j-1] > temp) ) {
                    xbuf[j] = xbuf[j-1];
                    j = j-1;
                }
                xbuf[j] = temp;
            } // End of X sort
#endif
            // Average the middle half of the  Y values and report them
            j = 0;
            for(i = (TPBUFSIZE/4) - 1; i < TPBUFSIZE - TPBUFSIZE/4; i++ ) {
                j += ybuf[i];
            }
            *y = lastY = j * (float)2/TPBUFSIZE;    // This is the average
            // Average the middle half of the  X values and report them
            j = 0;
            for(i = (TPBUFSIZE/4) - 1; i < TPBUFSIZE - TPBUFSIZE/4; i++ ) {
                j += xbuf[i];
            }
            *x = lastX = j * (float)2/TPBUFSIZE;    // This is the average
            // Tidy up and return
            if (touchState == touch || touchState == held)
                touchState = held;
            else
                touchState = touch;
            ret = touchState;
            touchSample = 0;             // Ready to start on the next set of data samples
        } else {
            // Buffer not yet full, so do not return any results yet
            if (touchState == touch || touchState == held) {
                *x = lastX;
                *y = lastY;
                ret = touchState = held;
            }
        }
        WriteCommand(INTC2, RA8875_INT_TP);            // reg INTC2: Clear that TP interrupt flag
    } // End of initial if -- data has been read and processed
    else {
        if (touchState == touch || touchState == held) {
            *x = lastX;
            *y = lastY;
            ret = touchState = held;
        } else if (touchState == release) {
            *x = lastX;
            *y = lastY;
            ret = release;
            touchState = no_touch;
        }
    }
    return ret;
}

/// The following section is derived from Carlos E. Vidales.
///
/// @copyright &copy; 2001, Carlos E. Vidales. All rights reserved.
///
///  This sample program was written and put in the public domain
///   by Carlos E. Vidales.  The program is provided "as is"
///   without warranty of any kind, either expressed or implied.
///  If you choose to use the program within your own products
///   you do so at your own risk, and assume the responsibility
///   for servicing, repairing or correcting the program should
///   it prove defective in any manner.
///  You may copy and distribute the program's source code in any
///   medium, provided that you also include in each copy an
///   appropriate copyright notice and disclaimer of warranty.
///  You may also modify this program and distribute copies of
///   it provided that you include prominent notices stating
///   that you changed the file(s) and the date of any change,
///   and that you do not charge any royalties or licenses for
///   its use.
///
///  This file contains functions that implement calculations
///   necessary to obtain calibration factors for a touch screen
///   that suffers from multiple distortion effects: namely,
///   translation, scaling and rotation.
///
///  The following set of equations represent a valid display
///   point given a corresponding set of touch screen points:
///
/// <pre>
///                                             /-     -\
///             /-    -\     /-            -\   |       |
///             |      |     |              |   |   Xs  |
///             |  Xd  |     | A    B    C  |   |       |
///             |      |  =  |              | * |   Ys  |
///             |  Yd  |     | D    E    F  |   |       |
///             |      |     |              |   |   1   |
///             \-    -/     \-            -/   |       |
///                                             \-     -/
///   where:
///          (Xd,Yd) represents the desired display point
///                   coordinates,
///          (Xs,Ys) represents the available touch screen
///                   coordinates, and the matrix
///          /-   -\
///          |A,B,C|
///          |D,E,F| represents the factors used to translate
///          \-   -/  the available touch screen point values
///                   into the corresponding display
///                   coordinates.
///   Note that for practical considerations, the utilities
///    within this file do not use the matrix coefficients as
///    defined above, but instead use the following
///    equivalents, since floating point math is not used:
///           A = An/Divider
///           B = Bn/Divider
///           C = Cn/Divider
///           D = Dn/Divider
///           E = En/Divider
///           F = Fn/Divider
///   The functions provided within this file are:
///         setCalibrationMatrix() - calculates the set of factors
///                                   in the above equation, given
///                                   three sets of test points.
///              getDisplayPoint() - returns the actual display
///                                   coordinates, given a set of
///                                   touch screen coordinates.
/// translateRawScreenCoordinates() - helper function to transform
///                                   raw screen points into values
///                                   scaled to the desired display
///                                   resolution.
///
///
///    Function: setCalibrationMatrix()
///
/// Description: Calling this function with valid input data
///               in the display and screen input arguments
///               causes the calibration factors between the
///               screen and display points to be calculated,
///               and the output argument - matrixPtr - to be
///               populated.
///
///              This function needs to be called only when new
///               calibration factors are desired.
///
///
/// Argument(s): displayPtr (input) - Pointer to an array of three
///                                    sample, reference points.
///              screenPtr (input) - Pointer to the array of touch
///                                   screen points corresponding
///                                   to the reference display points.
///              matrixPtr (output) - Pointer to the calibration
///                                    matrix computed for the set
///                                    of points being provided.
///
///
/// From the article text, recall that the matrix coefficients are
///  resolved to be the following:
///
///
///     Divider =  (Xs0 - Xs2)*(Ys1 - Ys2) - (Xs1 - Xs2)*(Ys0 - Ys2)
///
///
///
///                (Xd0 - Xd2)*(Ys1 - Ys2) - (Xd1 - Xd2)*(Ys0 - Ys2)
///           A = ---------------------------------------------------
///                                  Divider
///
///
///                (Xs0 - Xs2)*(Xd1 - Xd2) - (Xd0 - Xd2)*(Xs1 - Xs2)
///           B = ---------------------------------------------------
///                                  Divider
///
///
///                Ys0*(Xs2*Xd1 - Xs1*Xd2) +
///                            Ys1*(Xs0*Xd2 - Xs2*Xd0) +
///                                          Ys2*(Xs1*Xd0 - Xs0*Xd1)
///           C = ---------------------------------------------------
///                                  Divider
///
///
///                (Yd0 - Yd2)*(Ys1 - Ys2) - (Yd1 - Yd2)*(Ys0 - Ys2)
///           D = ---------------------------------------------------
///                                  Divider
///
///
///                (Xs0 - Xs2)*(Yd1 - Yd2) - (Yd0 - Yd2)*(Xs1 - Xs2)
///           E = ---------------------------------------------------
///                                  Divider
///
///
///                Ys0*(Xs2*Yd1 - Xs1*Yd2) +
///                            Ys1*(Xs0*Yd2 - Xs2*Yd0) +
///                                          Ys2*(Xs1*Yd0 - Xs0*Yd1)
///           F = ---------------------------------------------------
///                                  Divider
///
///
///      Return: OK - the calibration matrix was correctly
///                    calculated and its value is in the
///                    output argument.
///              NOT_OK - an error was detected and the
///                        function failed to return a valid
///                        set of matrix values.
///                       The only time this sample code returns
///                       NOT_OK is when Divider == 0
///
///
///
///                NOTE!    NOTE!    NOTE!
///
/// setCalibrationMatrix() and getDisplayPoint() will do fine
/// for you as they are, provided that your digitizer
/// resolution does not exceed 10 bits (1024 values).  Higher
/// resolutions may cause the integer operations to overflow
/// and return incorrect values.  If you wish to use these
/// functions with digitizer resolutions of 12 bits (4096
/// values) you will either have to a) use 64-bit signed
/// integer variables and math, or b) judiciously modify the
/// operations to scale results by a factor of 2 or even 4.
///
/// </pre>
///
RetCode_t RA8875::TouchPanelComputeCalibration(point_t * displayPtr, point_t * screenPtr, tpMatrix_t * matrixPtr)
{
    RetCode_t retValue = noerror;

    tpMatrix.Divider = ((screenPtr[0].x - screenPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) -
                       ((screenPtr[1].x - screenPtr[2].x) * (screenPtr[0].y - screenPtr[2].y)) ;

    if( tpMatrix.Divider == 0 )  {
        retValue = bad_parameter;
    }  else   {
        tpMatrix.An = ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) -
                      ((displayPtr[1].x - displayPtr[2].x) * (screenPtr[0].y - screenPtr[2].y)) ;

        tpMatrix.Bn = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].x - displayPtr[2].x)) -
                      ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].x - screenPtr[2].x)) ;

        tpMatrix.Cn = (screenPtr[2].x * displayPtr[1].x - screenPtr[1].x * displayPtr[2].x) * screenPtr[0].y +
                      (screenPtr[0].x * displayPtr[2].x - screenPtr[2].x * displayPtr[0].x) * screenPtr[1].y +
                      (screenPtr[1].x * displayPtr[0].x - screenPtr[0].x * displayPtr[1].x) * screenPtr[2].y ;

        tpMatrix.Dn = ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].y - screenPtr[2].y)) -
                      ((displayPtr[1].y - displayPtr[2].y) * (screenPtr[0].y - screenPtr[2].y)) ;

        tpMatrix.En = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].y - displayPtr[2].y)) -
                      ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].x - screenPtr[2].x)) ;

        tpMatrix.Fn = (screenPtr[2].x * displayPtr[1].y - screenPtr[1].x * displayPtr[2].y) * screenPtr[0].y +
                      (screenPtr[0].x * displayPtr[2].y - screenPtr[2].x * displayPtr[0].y) * screenPtr[1].y +
                      (screenPtr[1].x * displayPtr[0].y - screenPtr[0].x * displayPtr[1].y) * screenPtr[2].y ;
        touchState = no_touch;
        if (matrixPtr)
            memcpy(matrixPtr, &tpMatrix, sizeof(tpMatrix_t));
    }
    return( retValue ) ;
}

void RA8875::ResTouchPanelCfg(const char * _tpFQFN, const char * _tpCalMessage)
{
    tpFQFN = _tpFQFN;
    tpCalMessage = _tpCalMessage;
}


RetCode_t RA8875::_internal_ts_cal()
{
    FILE * fh;
    tpMatrix_t matrix;
    RetCode_t r = noerror;
    
    if (tpFQFN) {
        fh = fopen(tpFQFN, "rb");
        if (fh) {
            fread(&matrix, sizeof(tpMatrix_t), 1, fh);
            fclose(fh);
            TouchPanelSetMatrix(&matrix);
        } else {
            r = TouchPanelCalibrate(tpCalMessage, &matrix);
            if (r == noerror) {
                fh = fopen(tpFQFN, "wb");
                if (fh) {
                    fwrite(&matrix, sizeof(tpMatrix_t), 1, fh);
                    fclose(fh);
                    INFO("  tp cal written to '%s'.", tpFQFN);
                } else {
                    ERR("  couldn't open tpcal file '%s'.", tpFQFN);
                    r = file_not_found;
                }
            } else {
                ERR("error return: %d", r);
            }
            HexDump("TPCal", (const uint8_t *)&matrix, sizeof(tpMatrix_t));
            cls();
        }
    }
    return r;
}



////////////////// Capacitive Touch Panel

uint8_t RA8875::FT5206_ReadRegU8(uint8_t reg) {
    char val;
    
    m_i2c->write(m_addr, (const char *)&reg, 1);
    m_i2c->read(m_addr, &val, 1);
    return (uint8_t)val;
}

// Interrupt for touch detection
void RA8875::TouchPanelISR(void)
{
    if (useTouchPanel == TP_FT5206) {
        if (FT5206_TouchPositions())
            panelTouched = true;
    } else if (useTouchPanel == TP_GSL1680) {
        if (GSL1680_TouchPositions())
            panelTouched = true;
    }
}


// #### end of touch panel code additions
