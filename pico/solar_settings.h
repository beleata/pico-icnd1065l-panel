#pragma once
#include <stdint.h>
namespace solar {
struct Settings {
    uint32_t version=1,brightness=100,load=0x008cdc,export_color=0xd2b400,import_color=0xb40000;
    uint32_t swap_lower=0,rotate=0;
};
inline bool valid_settings(const Settings&s) {
    return s.version==1 && s.brightness>=1 && s.brightness<=100 &&
        s.load<=0xffffff && s.export_color<=0xffffff && s.import_color<=0xffffff &&
        s.swap_lower<=1 && s.rotate<=1;
}
}
