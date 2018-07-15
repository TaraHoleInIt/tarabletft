#ifndef _TTFT_FONT_H_
#define _TTFT_FONT_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TextAnchor_East = 0,
    TextAnchor_West,
    TextAnchor_North,
    TextAnchor_South,
    TextAnchor_NorthEast,
    TextAnchor_NorthWest,
    TextAnchor_SouthEast,
    TextAnchor_SouthWest,
    TextAnchor_Center
} TextAnchor;

struct TTFT_Device;

struct TTFT_FontDef {
    const char* FontName;
    const uint8_t* FontData;

    int Width;
    int Height;

    int StartChar;
    int EndChar;

    bool IsMonospace;
};

extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_9x12;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_11x12;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_15x17;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_19x24;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_24x25;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_33x39;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_42x50;
extern const struct TTFT_FontDef Font_Droid_Sans_Fallback_50x59;

extern const struct TTFT_FontDef Font_Droid_Sans_13x16;
extern const struct TTFT_FontDef Font_Droid_Sans_16x21;
extern const struct TTFT_FontDef Font_Droid_Sans_19x25;
extern const struct TTFT_FontDef Font_Droid_Sans_23x30;
extern const struct TTFT_FontDef Font_Droid_Sans_27x35;
extern const struct TTFT_FontDef Font_Droid_Sans_30x40;
extern const struct TTFT_FontDef Font_Droid_Sans_34x44;
extern const struct TTFT_FontDef Font_Droid_Sans_37x49;

extern const struct TTFT_FontDef Font_Liberation_Mono_11x19;
extern const struct TTFT_FontDef Font_Liberation_Mono_13x23;
extern const struct TTFT_FontDef Font_Liberation_Mono_17x29;
extern const struct TTFT_FontDef Font_Liberation_Mono_22x37;
extern const struct TTFT_FontDef Font_Liberation_Mono_26x46;
extern const struct TTFT_FontDef Font_Liberation_Mono_31x54;

extern const struct TTFT_FontDef Font_7Seg_32x64;
extern const struct TTFT_FontDef Font_7Seg_16x32;

extern const struct TTFT_FontDef Font_Char_16x22;

void TTFT_SetFont( struct TTFT_Device* DeviceHandle, const struct TTFT_FontDef* Font );
void TTFT_SetFontTransparent( struct TTFT_Device* DeviceHandle );
void TTFT_SetFontSolid( struct TTFT_Device* DeviceHandle );
void TTFT_SetFontProportional( struct TTFT_Device* DeviceHandle );
void TTFT_SetFontFixed( struct TTFT_Device* DeviceHandle );

int IRAM_ATTR TTFT_FontMeasureString( struct TTFT_Device* DeviceHandle, const char* String );
int IRAM_ATTR TTFT_FontDrawString( struct TTFT_Device* DeviceHandle, int x, int y, uint8_t FGColor, uint8_t BGColor, const char* String );
void IRAM_ATTR TTFT_FontDrawChar( struct TTFT_Device* DeviceHandle, char C, int x, int y, uint8_t FGColor, uint8_t BGColor );

void TTFT_FontGetAnchoredStringCoords( struct TTFT_Device* DeviceHandle, int* OutX, int* OutY, TextAnchor Anchor, const char* Text );
int TTFT_FontDrawAnchoredString( struct TTFT_Device* DeviceHandle, TextAnchor Anchor, const char* Text, uint8_t FGColor, uint8_t BGColor );

#ifdef __cplusplus
}
#endif

#endif
