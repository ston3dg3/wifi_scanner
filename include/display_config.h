#pragma once

// Set to 1 to build for the ILI9341 TFT (tft.cpp), 0 for the SSD1306 OLED
// (oled.cpp). Whichever is unselected compiles down to an empty
// translation unit, so its display library never gets linked in - the two
// backends are interchangeable at zero runtime cost.
#define USE_TFT_DISPLAY 1
