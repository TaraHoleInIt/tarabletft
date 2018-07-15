/**
 * Copyright (c) 2018 Tara Keeling
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "soc/spi_struct.h"
#include "esp_log.h"
#include "ttft.h"

static const int SPIFrequency = 40 * 1000000;
static const int MOSIPin = 23;
static const int MISOPin = 19;
static const int SCLKPin = 18;

static void IRAM_ATTR SwapInt( int* A, int* B );
static void IRAM_ATTR TTFT_PreTransferCallback( spi_transaction_t* Transaction );
static void TTFT_SetAddressWindow( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1 );
static void IRAM_ATTR TTFT_DrawWideLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color );
static void IRAM_ATTR TTFT_DrawTallLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color );

/*
 * SwapInt:
 * Swaps the values in the integers pointed to by A and B.
 */
static void IRAM_ATTR SwapInt( int* A, int* B ) {
    int Temp = *A;

    *A = *B;
    *B = Temp;
}

/*
 * TTFT_PreTransferCallback:
 * This manages the state of the data/command pin before SPI transfers.
 */
static void IRAM_ATTR TTFT_PreTransferCallback( spi_transaction_t* Transaction ) {
    int DCState = 0;
    int DCPin = 0;
    int User = 0;
    
    if ( Transaction != NULL ) {
        User = ( int ) Transaction->user;
        DCState = ( User >> 8 ) & 0xFF;
        DCPin = User & 0xFF;

        gpio_set_level( DCPin, DCState );
    }
}

/*
 * SPIMasterInit:
 * Initializes the SPI bus with default values.
 */
bool SPIMasterInit( void ) {
    const spi_bus_config_t SPIBusConfig = {
        .mosi_io_num = MOSIPin,
        .miso_io_num = MISOPin,
        .sclk_io_num = SCLKPin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_NATIVE_PINS,
        .max_transfer_sz = 16384 * 8
    };

    ESP_ERROR_CHECK_NONFATAL( spi_bus_initialize( VSPI_HOST, &SPIBusConfig, 1 ), return false );
    return true;
}

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
 */
bool TTFT_Init( struct TTFT_Device* DeviceHandle, int Width, int Height, int CSPin, int DCPin, int ResetPin, int BacklightPin, void ( *ResetProc ) ( struct TTFT_Device* ) ) {
    int Size = ( Width * Height );

    const spi_device_interface_config_t SPIDeviceConfig = {
        .clock_speed_hz = SPIFrequency,
        .spics_io_num = CSPin,
        .queue_size = 8,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .pre_cb = TTFT_PreTransferCallback
    };
    gpio_config_t IOOutputs = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT
    };

    NullCheck( DeviceHandle, return false );

    if ( DCPin == -1 ) {
        ESP_LOGE( __FUNCTION__, "Need a D/C pin to function properly!" );
        return false;
    }

    NullCheck( ( DeviceHandle->FrameBuffer = malloc( Size ) ), return false );
    memset( DeviceHandle->Palette, 0, sizeof( DeviceHandle->Palette ) );

    DeviceHandle->BacklightPin = BacklightPin;
    DeviceHandle->ResetPin = ResetPin;
    DeviceHandle->CSPin = CSPin;
    DeviceHandle->DCPin = DCPin;
    DeviceHandle->Width = Width;
    DeviceHandle->Height = Height;
    DeviceHandle->Handle = NULL;
    DeviceHandle->Font = NULL;
    DeviceHandle->FontGetGlyphWidth = NULL;

    IOOutputs.pin_bit_mask |= ( DCPin > -1 ) ? ( 1ULL << DCPin ) : 0;
    IOOutputs.pin_bit_mask |= ( ResetPin > -1 ) ? ( 1ULL << ResetPin ) : 0;
    IOOutputs.pin_bit_mask |= ( BacklightPin > -1 ) ? ( 1ULL << BacklightPin ) : 0;

    /* Set default values for gpio outputs */
    if ( ResetPin > -1 ) {
        gpio_set_level( ResetPin, 0 );
    }

    if ( BacklightPin > -1 ) {
        gpio_set_level( BacklightPin, 0 );
    }

    if ( DCPin > -1 ) {
        gpio_set_level( DCPin, 0 );
    }

    ESP_ERROR_CHECK_NONFATAL( gpio_config( &IOOutputs ), return false );
    ESP_ERROR_CHECK_NONFATAL( spi_bus_add_device( VSPI_HOST, &SPIDeviceConfig, &DeviceHandle->Handle ), return false );
    
    //TTFT_Reset_ILI9341( DeviceHandle );

    ResetProc( DeviceHandle );

    /* Turn on backlight if we control the pin */
    TTFT_SetBacklight( DeviceHandle, true );
    return true;
}

/*
 * TTFT_DeInit:
 * Frees memory used by the shadow framebuffer and zeroes out the device handle.
 */
void TTFT_DeInit( struct TTFT_Device* DeviceHandle ) {
    NullCheck( DeviceHandle, return );

    if ( DeviceHandle->FrameBuffer != NULL ) {
        heap_caps_free( DeviceHandle->FrameBuffer );
    }

    memset( DeviceHandle, 0, sizeof( struct TTFT_Device ) );
}

/*
 * TTFT_SetBacklight:
 * Turns the backlight on or off if a backlight control pin was given on init.
 * If not, this will have no effect.
 */
void TTFT_SetBacklight( struct TTFT_Device* DeviceHandle, bool On ) {
    NullCheck( DeviceHandle, return );

    if ( DeviceHandle->BacklightPin > -1 ) {
        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->BacklightPin, ( On == true ) ? 1 : 0 ), return );
    }
}

void TTFT_Reset_ST7735( struct TTFT_Device* DeviceHandle ) {
    NullCheck( DeviceHandle, return );

    if ( DeviceHandle->ResetPin > -1 ) {
        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 1 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );

        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 0 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );

        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 1 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );
    }

    /* Software reset */
    TTFT_SendCommand(
        DeviceHandle,
        0x01
    );

    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    /* Sleep out */
    TTFT_SendCommand(
        DeviceHandle,
        0x11
    );

    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    /* Gamma curve select */
    TTFT_SendCommand(
        DeviceHandle,
        0x26,
        0x04
    );

    /* Depth */
    TTFT_SendCommand(
        DeviceHandle,
        0x3A,
        PixelFormat
    );

    /* madctl */
    TTFT_SendCommand(
        DeviceHandle,
        0x36,
        0x00
    );

    /* Partial mode off */
    TTFT_SendCommand(
        DeviceHandle,
        0x13
    );
    
    /* Frame rate control */
    TTFT_SendCommand(
        DeviceHandle,
        0xB1,
        0x06,
        0x01,
        0x01
    );

    /* Display on */
    TTFT_SendCommand(
        DeviceHandle,
        0x29
    );
}

/*
 * TTFT_Reset:
 * First does a hardware reset (if pin configured), then a software reset,
 * then it does all of the necessary initialization commands to enable the display.
 */
void TTFT_Reset_ILI9341( struct TTFT_Device* DeviceHandle ) {
    NullCheck( DeviceHandle, return );

    if ( DeviceHandle->ResetPin > -1 ) {
        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 1 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );

        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 0 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );

        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( DeviceHandle->ResetPin, 1 ), return );
        vTaskDelay( pdMS_TO_TICKS( 150 ) );
    }

    /* 
     * TTFT Init sequence from fbcp-TTFT:
     * https://github.com/juj/fbcp-TTFT
     */

    /* Software reset plus 120ms delay */
    TTFT_SendCommand( 
        DeviceHandle, 
        0x01 
    );

    vTaskDelay( pdMS_TO_TICKS( 120 ) );

    /* Display off */
    TTFT_SendCommand( 
        DeviceHandle,
        0x28
    );

    /* Power control A */
    TTFT_SendCommand(
        DeviceHandle,
        0xCB,
        0x39,
        0x2C,
        0x00,
        0x34,
        0x02
    );

    /* Power control B */
    TTFT_SendCommand(
        DeviceHandle,
        0xCF,
        0x00,
        0xC1,
        0x30
    );

    /* Driver timing control A */
    TTFT_SendCommand(
        DeviceHandle,
        0xE8,
        0x85,
        0x00,
        0x78
    );

    /* Driver timing control B */
    TTFT_SendCommand(
        DeviceHandle,
        0xEA,
        0x00,
        0x00
    );

    /* Power on sequence control */
    TTFT_SendCommand(
        DeviceHandle,
        0xED,
        0x64,
        0x03,
        0x12,
        0x81
    );

    /* Power control 1 */
    TTFT_SendCommand( 
        DeviceHandle,
        0xC0,
        0x23
    );

    /* Power control 2 */
    TTFT_SendCommand( 
        DeviceHandle,
        0xC1,
        0x10
    );

    /* VCOM control 1 */
    TTFT_SendCommand( 
        DeviceHandle,
        0xC5,
        0x3E,
        0x28
    );

    /* VCOM control 2 */
    TTFT_SendCommand( 
        DeviceHandle,
        0xC7,
        0x86
    );

    /* madctl */
    TTFT_SendCommand( 
        DeviceHandle,
        0x36,
        0x00
    );

    /* Display inversion off */
    TTFT_SendCommand( 
        DeviceHandle,
        0x20
    );

    /* Pixel format */
    TTFT_SendCommand( 
        DeviceHandle,
        0x3A,
        PixelFormat
    );

    /* Frame rate control */
    TTFT_SendCommand( 
        DeviceHandle,
        0xB1,
        0x00,
        0x1B
    );

    /* Display function control */
    TTFT_SendCommand( 
        DeviceHandle,
        0xB6,
        0x08,
        0x82,
        0x27
    );

    /* Enable 3G */
    TTFT_SendCommand( 
        DeviceHandle,
        0xF2,
        0x02
    );

    /* Gamma set */
    TTFT_SendCommand( 
        DeviceHandle,
        0x26,
        0x01
    );

    /* Positive gamma correction */
    TTFT_SendCommand( 
        DeviceHandle,
        0xE0, 0x0F, 0x31, 0x2B,
        0x0C, 0x0E, 0x08, 0x4E,
        0xF1, 0x37, 0x07, 0x10,
        0x03, 0x0E, 0x09, 0x00
    );

    /* Negative gamma correction */
    TTFT_SendCommand( 
        DeviceHandle,
        0xE1, 0x00, 0x0E, 0x14,
        0x03, 0x11, 0x07, 0x31,
        0xC1, 0x48, 0x08, 0x0F,
        0x0C, 0x31, 0x36, 0x0F
    );

    /* Sleep out */
    TTFT_SendCommand( 
        DeviceHandle,
        0x11
    );

    /* 120ms delay */
    vTaskDelay( pdMS_TO_TICKS( 120 ) );

    /* Display on */
    TTFT_SendCommand( 
        DeviceHandle,
        0x29
    );
}

/*
 * TTFT_SetAddressWindow:
 * Enables RAM writes to the given address.
 */
static void IRAM_ATTR TTFT_SetAddressWindow( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1 ) {
    NullCheck( DeviceHandle, return );

    CheckBounds( x0, 0, x1, return );
    CheckBounds( x1, x0, DeviceHandle->Width - 1, return );

    CheckBounds( y0, 0, y1, return );
    CheckBounds( y1, y0, DeviceHandle->Height - 1, return );

    /* Set column address */
    TTFT_SendCommand( 
        DeviceHandle,
        0x2A,
        ( ( x0 >> 8 ) & 0xFF ),
        ( x0 & 0xFF ),
        ( ( x1 >> 8 ) & 0xFF ),
        ( x1 & 0xFF )
    );

    /* Set page address */
    TTFT_SendCommand( 
        DeviceHandle,
        0x2B,
        ( ( y0 >> 8 ) & 0xFF ),
        ( y0 & 0xFF ),
        ( ( y1 >> 8 ) & 0xFF ),
        ( y1 & 0xFF )
    );

    /* RAM write enable */
    TTFT_SendCommand( 
        DeviceHandle, 
        0x2C
    );
}

/*
 * TTFT_SetPalette:
 * Sets the given alette as the new palette used to convert from indexed colour during updates.
 */
void TTFT_SetPalette( struct TTFT_Device* DeviceHandle, const Color_t* NewPalette, size_t NewPaletteSize ) {
    NullCheck( DeviceHandle, return );
    NullCheck( NewPalette, return );

    memcpy( DeviceHandle->Palette, NewPalette, NewPaletteSize );
}

/*
 * TTFT_SetPaletteEntry:
 * Sets a single entry in the palette to the given RGB values.
 */
void TTFT_SetPaletteEntry( struct TTFT_Device* DeviceHandle, uint8_t Index, uint8_t Red, uint8_t Green, uint8_t Blue ) {
    Color_t Color = RGB( Red, Green, Blue );

    NullCheck( DeviceHandle, return );

    DeviceHandle->Palette[ Index ] = Color;
}

/*
 * TTFT_Clear:
 * Clears the entire screen with the given colour index.
 */
void TTFT_Clear( struct TTFT_Device* DeviceHandle, uint8_t Color ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    memset( DeviceHandle->FrameBuffer, Color, DeviceHandle->Width * DeviceHandle->Height );
}

/*
 * TTFT_PutPixel:
 * Draws a single pixel at the given x,y coordinates.
 */
void IRAM_ATTR TTFT_PutPixel( struct TTFT_Device* DeviceHandle, int x, int y, uint8_t Color ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    CheckBounds( x, 0, DeviceHandle->Width - 1, return );
    CheckBounds( y, 0, DeviceHandle->Height - 1, return );

    TTFT_SetPixel( DeviceHandle, x, y, Color );
}

/*
 * TTFT_DrawHLine:
 * Draws a horizontal line from (x0) to (x1)
 */
void IRAM_ATTR TTFT_DrawHLine( struct TTFT_Device* DeviceHandle, int x0, int y, int x1, uint8_t Color ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    CheckBounds( x0, 0, DeviceHandle->Width - 1, return );  // Start x coord is on screen?
    CheckBounds( x1, x0, DeviceHandle->Width - 1, return ); // End x coord is greater than start coord and on screen?
    CheckBounds( y, 0, DeviceHandle->Height - 1, return );  // Start y coord is on screen?

    for ( ; x0 <= x1; x0++ ) {
        TTFT_SetPixel( DeviceHandle, x0, y, Color );
    }
}

/*
 * TTFT_DrawVLine:
 * Draws a vertical line from (y0) to (y1)
 */
void IRAM_ATTR TTFT_DrawVLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int y1, uint8_t Color ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    CheckBounds( x0, 0, DeviceHandle->Width - 1, return );
    CheckBounds( y0, 0, DeviceHandle->Height - 1, return );
    CheckBounds( y1, y0, DeviceHandle->Height - 1, return );

    for ( ; y0 <= y1; y0++ ) {
        TTFT_SetPixel( DeviceHandle, x0, y0, Color );
    }
}

static void IRAM_ATTR TTFT_DrawWideLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color ) {
    int dx = ( x1 - x0 );
    int dy = ( y1 - y0 );
    int Error = 0;
    int Incr = 1;
    int x = x0;
    int y = y0;

    if ( dy < 0 ) {
        Incr = -1;
        dy = -dy;
    }

    Error = ( dy * 2 ) - dx;

    for ( ; x <= x1; x++ ) {
        TTFT_SetPixel( DeviceHandle, x, y, Color );

        if ( Error > 0 ) {
            Error-= ( dx * 2 );
            y+= Incr;
        }

        Error+= ( dy * 2 );
    }
}

static void IRAM_ATTR TTFT_DrawTallLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color ) {
    int dx = ( x1 - x0 );
    int dy = ( y1 - y0 );
    int Error = 0;
    int Incr = 1;
    int x = x0;
    int y = y0;

    if ( dx < 0 ) {
        Incr = -1;
        dx = -dx;
    }

    Error = ( dx * 2 ) - dy;

    for ( ; y < y1; y++ ) {
        TTFT_SetPixel( DeviceHandle, x, y, Color );

        if ( Error > 0 ) {
            Error-= ( dy * 2 );
            x+= Incr;
        }

        Error+= ( dx * 2 );
    }
}

/*
 * TTFT_DrawLine:
 * Draws a line between two points with the given colour index.
 */
void IRAM_ATTR TTFT_DrawLine( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color ) {
    NullCheck( DeviceHandle, return );

    CheckBounds( x0, 0, DeviceHandle->Width - 1, return );
    CheckBounds( y0, 0, DeviceHandle->Height - 1, return );

    if ( x0 == x1 ) {
        /* This is a vertical line, call the faster vertical line function instead */
        TTFT_DrawVLine( DeviceHandle, x0, y0, y1, Color );
    }
    else if ( y0 == y1 ) {
        /* This is a horizontal line, call the faster horizontal line function instead */
        TTFT_DrawHLine( DeviceHandle, x0, y0, x1, Color );
    }
    else {
        /* Sloping line */
        if ( abs( x1 - x0 ) > abs( y1 - y0 ) ) {
            if ( x0 > x1 ) {
                SwapInt( &x0, &x1 );
                SwapInt( &y0, &y1 );
            }

            TTFT_DrawWideLine( DeviceHandle, x0, y0, x1, y1, Color );
        }
        else {
            if ( y0 > y1 ) {
                SwapInt( &x0, &x1 );
                SwapInt( &y0, &y1 );
            }

            TTFT_DrawTallLine( DeviceHandle, x0, y0, x1, y1, Color );
        }
    }
}

/*
 * TTFT_FillRect:
 * Fills a section of the screen with the given colour.
 */
void IRAM_ATTR TTFT_FillRect( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, uint8_t Color ) {
    int Width = ( x1 - x0 ) + 1;
    int x = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    CheckBounds( x0, 0, DeviceHandle->Width - 1, return );
    CheckBounds( y0, 0, DeviceHandle->Height - 1, return );

    CheckBounds( x1, x0, DeviceHandle->Width - 1, return );
    CheckBounds( y1, y0, DeviceHandle->Height - 1, return );

    for ( ; y0 <= y1; y0++ ) {
        for ( x = x0; x < ( x0 + Width ); x++ ) {
            TTFT_SetPixel( DeviceHandle, x, y0, Color );
        }
    }
}

/*
 * TTFT_DrawBox:
 * Draws a box using the given coordinates filling by (Thickness) inwards.
 */
void IRAM_ATTR TTFT_DrawBox( struct TTFT_Device* DeviceHandle, int x0, int y0, int x1, int y1, int Thickness, uint8_t Color ) {
    int i = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    CheckBounds( x0, 0, DeviceHandle->Width - 1, return );
    CheckBounds( y0, 0, DeviceHandle->Height - 1, return );

    CheckBounds( x1, x0, DeviceHandle->Width - 1, return );
    CheckBounds( y1, y0, DeviceHandle->Height - 1, return );

    for ( i = 0; i < Thickness; i++ ) {
        /* Top */
        TTFT_DrawHLine( DeviceHandle, x0, y0 + i, x1, Color );

        /* Bottom */
        TTFT_DrawHLine( DeviceHandle, x0, y1 - i, x1, Color );

        /* Left */
        TTFT_DrawVLine( DeviceHandle, x0 + i, y0, y1, Color );

        /* Right */
        TTFT_DrawVLine( DeviceHandle, x1 - i, y0, y1, Color );
    }
}

void TTFT_SPIWrite( struct TTFT_Device* DeviceHandle, const uint8_t* Data, size_t DataLength, bool IsCommand ) {
    spi_transaction_t SPITrans;
    int User = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( Data, return );

    if ( DataLength > 0 ) {
        memset( &SPITrans, 0, sizeof( SPITrans ) );

        User = ( IsCommand == false ) ? ( 1 << 8 ) : 0;
        User |= DeviceHandle->DCPin;

        SPITrans.length = DataLength * 8;
        SPITrans.user = ( void* ) MakeUser( DeviceHandle->DCPin, ( IsCommand == true ) ? 0 : 1 );
        SPITrans.tx_buffer = Data;

        ESP_ERROR_CHECK_NONFATAL( spi_device_transmit( DeviceHandle->Handle, &SPITrans ), return );
    }
}

#define LineUpdateCount 4

/*
 * TTFT_Update:
 * Converts the 8bit indexed shadow framebuffer to RGB (LineUpdateCount)
 * lines at a time and sends it out over the SPI bus.
 * 
 * Note:
 * A higher LineUpdateCount might speed things up but will use more memory.
 */
void IRAM_ATTR TTFT_Update( struct TTFT_Device* DeviceHandle ) {
    Color_t* LineBuffer = NULL;
    uint8_t* Ptr = NULL;
    int LineWidth = 0;
    int LineSize = 0;
    int i = 0;
    int y = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->FrameBuffer, return );

    LineWidth = DeviceHandle->Width;
    LineSize = LineWidth * LineUpdateCount * sizeof( Color_t );

    NullCheck( ( LineBuffer = heap_caps_malloc( LineSize, MALLOC_CAP_DMA ) ), return );

    TTFT_SetAddressWindow( DeviceHandle, 0, 0, DeviceHandle->Width - 1, DeviceHandle->Height - 1 );
    Ptr = DeviceHandle->FrameBuffer;

    for ( y = 0; y < DeviceHandle->Height; y+= LineUpdateCount ) {
        for ( i = 0; i < LineWidth * LineUpdateCount; i++ ) {
            LineBuffer[ i ] = DeviceHandle->Palette[ *Ptr++ ];
        }

        TTFT_SPIWrite( DeviceHandle, ( const uint8_t* ) LineBuffer, LineSize, false );
    }

    heap_caps_free( LineBuffer );
}
