#pragma once
#include "ota_journal.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
namespace ota {
struct Flash {
    const uint8_t *ptr(uint32_t offset)const {return reinterpret_cast<const uint8_t*>(XIP_BASE+offset);}
    bool erase(uint32_t offset,uint32_t size) {
        if(offset%SECTOR || size%SECTOR || offset<APP || offset+size>0x200000)return false;
        uint32_t irq=save_and_disable_interrupts();flash_range_erase(offset,size);restore_interrupts(irq);return true;
    }
    bool program(uint32_t offset,const uint8_t *data,uint32_t size) {
        if(offset%PAGE || size%PAGE || offset<APP || offset+size>0x200000)return false;
        uint32_t irq=save_and_disable_interrupts();flash_range_program(offset,data,size);restore_interrupts(irq);return true;
    }
};
inline Flash flash;
}
