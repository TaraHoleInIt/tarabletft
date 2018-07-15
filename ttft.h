#ifndef _TTFT_H_
#define _TTFT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#define REG_MADCTL 0x36

/* Flipped vertically and horizontally */
#define MADCTL_MY BIT( 7 )

/* Flipped horizontally */
#define MADCTL_MX BIT( 6 )

/* Column and row exchange.
 * ie. 320x240 becomes 240x320 and vice versa.
 * Rotated 90 degrees clockwise, flipped horizontally.
 */
#define MADCTL_MV BIT( 5 )

/* Appears correct on m5 stack */
#define MADCTL_ML BIT( 4 )

/* Self explanatory */
#define MADCTL_BGR BIT( 3 )

/* No effect on m5 stack */
#define MADCTL_MH BIT( 2 )

#if ! defined NullCheck
    #define NullCheck( ptr, retexpr ) { \
        if ( ptr == NULL ) { \
            ESP_LOGE( __FUNCTION__, "%s == NULL", #ptr ); \
            retexpr; \
        } \
    }
#endif

#if ! defined ESP_ERROR_CHECK_NONFATAL
    #define ESP_ERROR_CHECK_NONFATAL( expr, retexpr ) { \
        esp_err_t __err_rc = ( expr ); \
        if ( __err_rc != ESP_OK ) { \
            ESP_LOGE( __FUNCTION__, "%s != ESP_OK, result: %d", #expr, __err_rc ); \
            retexpr; \
        } \
    }
#endif

#if ! defined CheckBounds
    #define CheckBounds( val, min, max, retexpr ) { \
        if ( ( val ) < ( min ) ) { \
            ESP_LOGE( __func__, "%s (%d) < %s (%d)", #val, val, #min, min ); \
            retexpr; \
        } \
        if ( ( val ) > ( max ) ) { \
            ESP_LOGE( __func__, "%s (%d) > %s (%d)", #val, val, #max, max ); \
            retexpr; \
        } \
    }
#endif

#define TTFT_SendCommand( DeviceHandle, Command, ... ) { \
    do { \
        const uint8_t Data[ ] = { __VA_ARGS__ }; \
        const uint8_t CMD = Command; \
        \
        TTFT_SPIWrite( DeviceHandle, &CMD, sizeof( uint8_t ), true ); \
        TTFT_SPIWrite( DeviceHandle, Data, sizeof( Data ), false ); \
    } while ( false ); \
}

#define MakeUser( Pin, Command ) ( ( Pin | ( Command << 8 ) ) )

//#define _18BIT_COLOR

#if defined _18BIT_COLOR
    typedef struct __attribute__( ( packed ) ) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } Color_t;

    #define PixelFormat 0x66

    #define RGB( r, g, b ) { \
        ( r & 0xFF ), \
        ( g & 0xFF ), \
        ( b & 0xFF ) \
    }
#else
    typedef uint16_t Color_t;

    #define PixelFormat 0x55

    #define RGB( r, g, b ) __builtin_bswap16( \
        ( ( ( r & 0xFF ) >> 3 ) << 11 ) | \
        ( ( ( g & 0xFF ) >> 2 ) << 5 ) | \
        ( ( b & 0xFF ) >> 3 ) \
    )
#endif

#define TTFT_SetPixel( DeviceHandle, x, y, Color ) { \
    do { \
        if ( Color != 255 ) { \
            DeviceHandle->FrameBuffer[ ( x ) + ( ( y ) * DeviceHandle->Width ) ] = Color; \
        } \
    } while ( false ); \
} 

struct TTFT_FontDef;

struct TTFT_Device {
    int BacklightPin;
    int ResetPin;
    int CSPin;
    int DCPin;

    int Width;
    int Height;

    spi_device_handle_t Handle;

    uint8_t* FrameBuffer;
    Color_t Palette[ 256 ];

    int ( *FontGetGlyphWidth ) ( const struct TTFT_FontDef*, char );
    const struct TTFT_FontDef* Font;
};

/*
 * SPIMasterInit:
 * Initializes the SPI bus with the given pins.
 */
bool SPIMasterInit( int MOSIPin, int MISOPin, int SCLKPin );

/*
 * Initializes and resets the LCD device connected to the given GPIO pins.
 * 
 * Required parameters:
 * Width:   Width of the Display
 * Height:  Height of the display
 * DCPin:   Data/command selection pin
 * 
 * Optional parameters:
 * CSPin:           Can be held low if nothing else will be on the SPI bus
 * ResetPin:        Can be held high to skip hardware reset, TTFT_Reset will still reset the device in software.
 * BacklightPin:    Can be held high to be always on or if you're going to manage it yourself.
 * ResetProc:       Pointer to device specific reset function which should send commands needed to initialize the display.
 * SPIFrequency:    Frequency in Hz to drive the SPI display at.
 */
bool TTFT_Init( struct TTFT_Device* DeviceHandle, int Width, int Height, int CSPin, int DCPin, int ResetPin, int BacklightPin, void ( *ResetProc ) ( struct TTFT_Device* ), int SPIFrequency );

/*
 * TTFT_DeInit:
 * Frees memory used by the shadow framebuffer and zeroes out the device handle.
 */
void TTFT_DeInit( struct TTFT_Device* DeviceHandle );

/*
 * TTFT_SetBacklight:
 * Turns the backlight on or off if a backlight control pin was given on init.
 * If not, this will have no effect.
 */
void TTFT_SetBacklight( struct TTFT_Device* DeviceHandle, bool On );

/*
 * TTFT_Reset_ILI9341:
 * Reset and init sequency for the ST7735 display controller.
 */
void TTFT_Reset_ILI9341( struct TTFT_Device* DeviceHandle );

/*
 * TTFT_Reset_ST7735:
 * Reset and init sequence for the ST7735 display controller.
 */
void TTFT_Reset_ST7735( struct TTFT_Device* DeviceHandle );

/*
 * TTFT_SetPalette:
 * Sets the given alette as the new palette used to convert from indexed colour during updates.
 */
void TTFT_SetPalette( struct TTFT_Device* DeviceHandle, const Color_t* NewPalette, size_t NewPaletteSize );

/*
 * TTFT_SetPaletteEntry:
 * Sets a single entry in the palette to the given RGB values.
 */
void TTFT_SetPaletteEntry( struct TTFT_Device* DeviceHandle, uint8_t Index, uint8_t Red, uint8_t Green, uint8_t Blue );

/*
 * TTFT_Clear:
 * Clears the entire screen with the given colour index.
 */
void TTFT_Clear( struct TTFT_Device* DeviceHandle, uint8_t Color );

/*
 * TTFT_PutPixel:
 * Draws a single pixel at the given x,y coordinates.
 */
void IRAM_ATTR TTFT_PutPixel( struct TTFT_Device* DeviceHandle, int x, int y, uint8_t Color );

/*
 * TTFT_DrawHLine:
 * Draws a horizontal line from (x0) to (x1)
 */
void IRAM_ATTR TTFT_DrawHLine( struct TTFT_Device* DeviceHandle, int x0, int y, int x1, uint8_t Color );

/*
 * TTFT_DrawVLine:
 * Draws a vertical line from (y0) to (y1)
 */
void IRAM_ATTR TTFT_DrawVLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int y1, uint8_t Color );

/*
 * TTFT_DrawLine:
 * Draws a line between two points with the given colour index.
 */
void IRAM_ATTR TTFT_DrawLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color );

/*
 * TTFT_FillRect:
 * Fills a section of the screen with the given colour.
 */
void IRAM_ATTR TTFT_FillRect( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color );

/*
 * TTFT_DrawBox:
 * Draws a box using the given coordinates filling by (Thickness) inwards.
 */
void IRAM_ATTR TTFT_DrawBox( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, int Thickness, uint8_t Color );

/*
 * TTFT_Update:
 * Converts the 8bit indexed shadow framebuffer to RGB565 (LineUpdateCount)
 * lines at a time and sends it out over the SPI bus.
 * 
 * Note:
 * A higher LineUpdateCount might speed things up but will use more memory.
 */
void IRAM_ATTR TTFT_Update( struct TTFT_Device* DeviceHandle );

void TTFT_SPIWrite( struct TTFT_Device* DeviceHandle, const uint8_t* Data, size_t DataLength, bool IsCommand );

#if 0
static __attribute__( ( always_inline ) ) bool IsPixelVisible( struct ILI9341_Device* DeviceHandle, int x, int y ) {
    return x >= 0 && y >= 0 && x < DeviceHandle->Width && y < DeviceHandle->Height;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
