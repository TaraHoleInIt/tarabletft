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
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ttft.h"
#include "ttft_font.h"

static bool IsCharacterInFont( const struct TTFT_FontDef* Font, char C ) {
    NullCheck( Font, return false );
    
    return ( ( C >= Font->StartChar ) && ( C <= Font->EndChar ) );
}

static int RoundUpFontHeight( int Height ) {
    if ( ( Height % 8 ) != 0 ) {
        return ( ( Height + 7 ) / 8 ) * 8;
    }

    return Height;
}

static const uint8_t* GetGlyphPtr( const struct TTFT_FontDef* Font, char C ) {
    NullCheck( Font, return NULL );
    NullCheck( Font->FontData, return NULL );

    CheckBounds( C, Font->StartChar, Font->EndChar, return NULL );

    return &Font->FontData[ ( C - Font->StartChar ) * ( ( Font->Width * ( RoundUpFontHeight( Font->Height ) / 8 ) ) + 1 ) ];
}

static int GetGlyphWidthProportional( const struct TTFT_FontDef* Font, char C ) {
    const uint8_t* GlyphData = NULL;

    NullCheck( Font, return 0 );
    NullCheck( ( GlyphData = GetGlyphPtr( Font, C ) ), return 0 );

    return ( int ) ( *GetGlyphPtr( Font, C ) );
}

static int GetGlyphWidthFixed( const struct TTFT_FontDef* Font, char C ) {
    return Font->Width;
}

void TTFT_SetFont( struct TTFT_Device* DeviceHandle, const struct TTFT_FontDef* Font ) {
    NullCheck( DeviceHandle, return );
    NullCheck( Font, return );

    DeviceHandle->FontGetGlyphWidth = ( Font->IsMonospace == true ) ? GetGlyphWidthFixed : GetGlyphWidthProportional;
    DeviceHandle->Font = Font;
}

void TTFT_SetFontProportional( struct TTFT_Device* DeviceHandle ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->Font, return );

    DeviceHandle->FontGetGlyphWidth = GetGlyphWidthProportional;
}

void TTFT_SetFontFixed( struct TTFT_Device* DeviceHandle ) {
    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->Font, return );

    DeviceHandle->FontGetGlyphWidth = GetGlyphWidthFixed;
}

void IRAM_ATTR TTFT_FontDrawChar( struct TTFT_Device* DeviceHandle, char C, int x, int y, uint8_t FGColor, uint8_t BGColor ) {
    const uint8_t* GlyphData = NULL;
    int GlyphColumnLen = 0;
    int CharStartX =  0;
    int CharStartY = 0;
    int CharWidth = 0;
    int CharHeight = 0;
    int CharEndX = 0;
    int CharEndY = 0;
    int OffsetX = 0;
    int OffsetY = 0;
    int YByte = 0;
    int YBit = 0;
    int i = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( DeviceHandle->Font, return );
    
    if ( IsCharacterInFont( DeviceHandle->Font, C ) == true ) {
        NullCheck( ( GlyphData = GetGlyphPtr( DeviceHandle->Font, C ) ), return );

        /* The first byte in the glyph data is the width of the character in pixels, skip over */
        GlyphData++;
        GlyphColumnLen = RoundUpFontHeight( DeviceHandle->Font->Height ) / 8;
        
        CharWidth = DeviceHandle->FontGetGlyphWidth( DeviceHandle->Font, C );
        CharHeight = DeviceHandle->Font->Height;

        CharStartX = x;
        CharStartY = y;
        
        CharEndX = CharStartX + CharWidth;
        CharEndY = CharStartY + CharHeight;

        /* If the character is partially offscreen offset the end by
        * distance between (coord) and 0.
        */
        OffsetX = ( CharStartX < 0 ) ? abs( CharStartX ) : 0;
        OffsetY = ( CharStartY < 0 ) ? abs( CharStartY ) : 0;

        /* This skips into the proper column within the glyph data */
        GlyphData+= ( OffsetX * GlyphColumnLen );

        CharStartX+= OffsetX;
        CharStartY+= OffsetY;

        /* Do not attempt to draw if this character is entirely offscreen */
        if ( CharEndX < 0 || CharStartX >= DeviceHandle->Width || CharEndY < 0 || CharStartY >= DeviceHandle->Height ) {
            //ClipDebug( x, y );
            return;
        }

        /* Do not attempt to draw past the end of the screen */
        CharEndX = ( CharEndX >= DeviceHandle->Width ) ? DeviceHandle->Width - 1 : CharEndX;
        CharEndY = ( CharEndY >= DeviceHandle->Height ) ? DeviceHandle->Height - 1 : CharEndY;

        for ( x = CharStartX; x < CharEndX; x++ ) {
            for ( y = CharStartY, i = 0; y < CharEndY && i < CharHeight; y++, i++ ) {
                YByte = ( i + OffsetY ) / 8;
                YBit = ( i + OffsetY ) & 0x07;

                if ( GlyphData[ YByte ] & BIT( YBit ) ) {
                    TTFT_SetPixel( DeviceHandle, x, y, FGColor );
                } else {
                    TTFT_SetPixel( DeviceHandle, x, y, BGColor );
                } 
            }

            GlyphData+= GlyphColumnLen;
        }
    }
}

int IRAM_ATTR TTFT_FontMeasureString( struct TTFT_Device* DeviceHandle, const char* String ) {
    int StringLengthPixels = 0;
    int StringLengthChars = 0;
    int i = 0;

    NullCheck( DeviceHandle, return 0 );
    NullCheck( DeviceHandle->FrameBuffer, return 0 );
    NullCheck( DeviceHandle->Font, return 0 );
    NullCheck( String, return 0 );

    if ( ( StringLengthChars = strlen( String ) ) > 0 ) {
        for ( i = 0; i < StringLengthChars; i++ ) {
            if ( IsCharacterInFont( DeviceHandle->Font, String[ i ] ) == true ) {
                StringLengthPixels+= DeviceHandle->FontGetGlyphWidth( DeviceHandle->Font, String[ i ] );
            }
        }
    }

    return StringLengthPixels;
}

int IRAM_ATTR TTFT_FontDrawString( struct TTFT_Device* DeviceHandle, int x, int y, uint8_t FGColor, uint8_t BGColor, const char* Text ) {
    int StringLengthPixels = 0;
    int StringLengthChars = 0;
    int SavedX = x;
    int i = 0;

    NullCheck( DeviceHandle, return 0 );
    NullCheck( DeviceHandle->FrameBuffer, return 0 );
    NullCheck( DeviceHandle->Font, return 0 );
    NullCheck( Text, return 0 );

    if ( ( StringLengthPixels = TTFT_FontMeasureString( DeviceHandle, Text ) ) > 0 ) {
        StringLengthChars = strlen( Text );

        for ( i = 0; i < StringLengthChars; i++ ) {
            if ( Text[ i ] == '\n' ) {
                y+= DeviceHandle->Font->Height;
                x = SavedX;

                continue;
            }

            if ( IsCharacterInFont( DeviceHandle->Font, Text[ i ] ) == true ) {
                TTFT_FontDrawChar( DeviceHandle, Text[ i ], x, y, FGColor, BGColor );
                x+= DeviceHandle->FontGetGlyphWidth( DeviceHandle->Font, Text[ i ] );
            }
        }

        return x;
    }

    return 0;
}

int TTFT_FontDrawAnchoredString( struct TTFT_Device* DeviceHandle, TextAnchor Anchor, const char* Text, uint8_t FGColor, uint8_t BGColor ) {
    int x = 0;
    int y = 0;

    NullCheck( DeviceHandle, return 0 );
    NullCheck( Text, return 0 );

    TTFT_FontGetAnchoredStringCoords( DeviceHandle, &x, &y, Anchor, Text );
    return TTFT_FontDrawString( DeviceHandle, x, y, FGColor, BGColor, Text );
}

void TTFT_FontGetAnchoredStringCoords( struct TTFT_Device* DeviceHandle, int* OutX, int* OutY, TextAnchor Anchor, const char* Text ) {
    int StringWidth = 0;
    int StringHeight = 0;

    NullCheck( DeviceHandle, return );
    NullCheck( OutX, return );
    NullCheck( OutY, return );
    NullCheck( Text, return );

    StringWidth = TTFT_FontMeasureString( DeviceHandle, Text );
    StringHeight = DeviceHandle->Font->Height;

    switch ( Anchor ) {
        case TextAnchor_East: {
            *OutY = ( DeviceHandle->Height / 2 ) - ( StringHeight / 2 );
            *OutX = ( DeviceHandle->Width - StringWidth );

            break;
        }
        case TextAnchor_West: {
            *OutY = ( DeviceHandle->Height / 2 ) - ( StringHeight / 2 );
            *OutX = 0;

            break;
        }
        case TextAnchor_North: {
            *OutX = ( DeviceHandle->Width / 2 ) - ( StringWidth / 2 );
            *OutY = 0;

            break;
        }
        case TextAnchor_South: {
            *OutX = ( DeviceHandle->Width / 2 ) - ( StringWidth / 2 );
            *OutY = ( DeviceHandle->Height - StringHeight );
            
            break;
        }
        case TextAnchor_NorthEast: {
            *OutX = ( DeviceHandle->Width - StringWidth );
            *OutY = 0;

            break;
        }
        case TextAnchor_NorthWest: {
            *OutY = 0;
            *OutX = 0;

            break;
        }
        case TextAnchor_SouthEast: {
            *OutY = ( DeviceHandle->Height - StringHeight );
            *OutX = ( DeviceHandle->Width - StringWidth );

            break;
        }
        case TextAnchor_SouthWest: {
            *OutY = ( DeviceHandle->Height - StringHeight );
            *OutX = 0;

            break;
        }
        case TextAnchor_Center: {
            *OutY = ( DeviceHandle->Height / 2 ) - ( StringHeight / 2 );
            *OutX = ( DeviceHandle->Width / 2 ) - ( StringWidth / 2 );

            break;
        }
        default: {
            *OutX = DeviceHandle->Width;
            *OutY = DeviceHandle->Height;
            
            break;
        }
    };
}
