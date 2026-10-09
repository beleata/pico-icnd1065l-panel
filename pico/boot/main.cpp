// Jump sequence derived from JZimnol/pico_fota_bootloader (MIT); linker/LICENSE.
#include "ota_flash.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/resets.h"
#include "RP2040.h"

static uint8_t sector[ota::SECTOR];
static void fail() { reset_usb_boot(0,0); }
int main() {
    watchdog_disable();
    // Keep the panel blank while flash images are exchanged.
    gpio_init(13);gpio_set_dir(13,true);gpio_put(13,true);
    auto s=ota::state(ota::flash);
    if(s.mode==ota::TRIAL) {
        s.mode=ota::ROLLBACK;s.index=0;s.phase=0;
        if(!ota::save(ota::flash,s))fail();
    }
    while(s.mode==ota::UPDATE || s.mode==ota::ROLLBACK) {
        if(!ota::swap_step(ota::flash,s,sector))fail();
    }
    if(s.mode!=ota::IDLE && s.mode!=ota::TRIAL)fail();
    auto vectors=reinterpret_cast<const uint32_t*>(XIP_BASE+ota::APP);
    if(vectors[0]<0x20000000 || vectors[0]>0x20042000 || !(vectors[1]&1) ||
       vectors[1]<XIP_BASE+ota::APP || vectors[1]>=XIP_BASE+ota::APP+ota::SLOT)fail();
    // A candidate which never reaches main must also return to the old image.
    watchdog_enable(8000,true);
    SysTick->CTRL=0;NVIC->ICER[0]=0xffffffff;NVIC->ICPR[0]=0xffffffff;
    reset_block(~(RESETS_RESET_IO_QSPI_BITS|RESETS_RESET_PADS_QSPI_BITS|RESETS_RESET_SYSCFG_BITS|RESETS_RESET_PLL_SYS_BITS));
    SCB->VTOR=XIP_BASE+ota::APP;
    asm volatile("msr msp, %0\n bx %1" :: "r"(vectors[0]),"r"(vectors[1]));
    __builtin_unreachable();
}
