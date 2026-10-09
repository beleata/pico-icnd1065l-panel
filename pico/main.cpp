#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/stdio_usb.h"
#include "pico/stdio/driver.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "video_frame.h"
#include "panel_wave.pio.h"
#ifdef PICO_PANEL_CHAIN_TEST
#include "chain_wave_data.h"
#else
#include "wave_data.h"
#endif
#ifdef PICO_PANEL_CHAIN_ARROW
#include "chain_arrow.h"
#endif
#ifdef PICO_PANEL_SOLAR
#include "solar_app.h"
#endif

static constexpr uint BASE_PIN = 2;
static constexpr uint PIN_COUNT = 12;
#ifdef PICO_DEFAULT_LED_PIN
static constexpr uint LED_PIN = PICO_DEFAULT_LED_PIN;
#endif
struct alignas(16) DmaBlock {
    uint32_t read_address, write_address, count, control;
};
static DmaBlock blocks[PLAN_COUNT];
#ifdef PICO_PANEL_CHAIN_ARROW
static_assert(PLAN_COUNT>=4099,"Arrow reuses startup descriptor memory");
#endif
static DmaBlock idle_plan[1], update_plan[38];
static uint32_t next_plan;
static uint8_t frames[2][video::FRAME_BYTES];
static int data_channel;
// Only the DMA IRQ writes STARTUP -> IDLE, QUEUED -> RUNNING -> COMPLETE.
enum Phase { STARTUP, IDLE, QUEUED, RUNNING, COMPLETE };
static volatile Phase phase = STARTUP;
#ifdef PICO_PANEL_SOLAR
static uint solar_sm,solar_offset;
static int solar_control,solar_loop;
static bool solar_paused=false,solar_redraw=false;
static void panel_pause(bool paused) {
    if(paused==solar_paused)return;
    solar_paused=paused;
    uint32_t irq=save_and_disable_interrupts();
    if(paused) {
        gpio_set_outover(13,GPIO_OVERRIDE_HIGH);
        pio_sm_set_enabled(pio0,solar_sm,false);
        for(int ch:{data_channel,solar_control,solar_loop})hw_clear_bits(&dma_hw->ch[ch].ctrl_trig,DMA_CH0_CTRL_TRIG_EN_BITS);
        for(int ch:{data_channel,solar_control,solar_loop})dma_channel_abort(ch);
        dma_channel_acknowledge_irq0(data_channel);
    } else {
        phase=IDLE;next_plan=uint32_t(uintptr_t(idle_plan));
        pio_sm_clear_fifos(pio0,solar_sm);pio_sm_restart(pio0,solar_sm);
        pio_sm_exec(pio0,solar_sm,pio_encode_jmp(solar_offset));
        hw_set_bits(&dma_hw->ch[solar_loop].ctrl_trig,DMA_CH0_CTRL_TRIG_EN_BITS);
        hw_set_bits(&dma_hw->ch[solar_control].ctrl_trig,DMA_CH0_CTRL_TRIG_EN_BITS);
        dma_channel_set_write_addr(solar_control,&dma_hw->ch[data_channel].read_addr,false);
        dma_channel_set_trans_count(solar_control,4,false);
        dma_channel_set_read_addr(solar_control,idle_plan,true);
        while(!pio_sm_is_tx_fifo_full(pio0,solar_sm))tight_loop_contents();
        pio0->fdebug=1u<<(PIO_FDEBUG_TXSTALL_LSB+solar_sm);
        pio_sm_set_enabled(pio0,solar_sm,true);
        gpio_set_outover(13,GPIO_OVERRIDE_NORMAL);solar_redraw=true;
    }
    restore_interrupts(irq);
}
#endif

static void __not_in_flash_func(dma_complete)() {
    dma_channel_acknowledge_irq0(data_channel);
    if (phase == STARTUP) phase = IDLE;
    else if (phase == QUEUED) {
        next_plan = uint32_t(uintptr_t(idle_plan));
        __dmb();
        phase = RUNNING;
    } else if (phase == RUNNING) phase = COMPLETE;
}

int main() {
    // Match the FPGA's 320 ns half-clock exactly, without overclocking.
    set_sys_clock_khz(125000, true);
    stdio_init_all();
#ifdef PICO_PANEL_SOLAR
    ota::init();
#ifdef PICO_PANEL_CHAIN_ARROW
    chain_arrow::init();
#endif
#ifdef PANEL_OTA_TEST_CRASH
    if(ota::trial)while(true)tight_loop_contents();
#endif
    // Replace the demo before DMA can read it; no invented solar measurements.
#ifndef PICO_PANEL_CHAIN_TEST
    solar_app::frame(frames[0]);
    video::render(frames[0],wave_visible);
#endif
#endif
    bi_decl(bi_program_description("RP2040 ICND1065L USB video RAM"));
    bi_decl(bi_1pin_with_name(11, "Panel SCLK"));
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &panel_wave_program);
    pio_sm_config config = panel_wave_program_get_default_config(offset);
    sm_config_set_out_pins(&config, BASE_PIN, PIN_COUNT);
    sm_config_set_out_shift(&config, true, true, 16);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, 1.0f);
    for (uint pin=BASE_PIN; pin<BASE_PIN+PIN_COUNT; ++pin) {
        pio_gpio_init(pio, pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
    }
    pio_sm_init(pio, sm, offset, &config);
    pio_sm_set_pins_with_mask(pio, sm, 0, ((1u<<PIN_COUNT)-1)<<BASE_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, BASE_PIN, PIN_COUNT, true);

    int data = dma_claim_unused_channel(true);
    data_channel = data;
    int control = dma_claim_unused_channel(true);
    int loop = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(data);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    channel_config_set_high_priority(&dc, true);
    channel_config_set_chain_to(&dc, control);
    for (size_t i=0; i<PLAN_COUNT; ++i) {
#ifdef PICO_PANEL_CHAIN_TEST
        channel_config_set_ring(&dc,false,startup_plan[i].ring_bits);
#endif
        if (i == PLAN_COUNT-1) channel_config_set_chain_to(&dc, loop);
        channel_config_set_irq_quiet(&dc, i != PLAN_COUNT-1);
        blocks[i] = {uint32_t(uintptr_t(startup_plan[i].samples)),
                     uint32_t(uintptr_t(&pio->txf[sm])), startup_plan[i].count,
                     channel_config_get_ctrl_value(&dc)};
    }

    auto descriptor = [&](const uint16_t *samples, uint32_t count, bool last, bool interrupt) {
        channel_config_set_chain_to(&dc, last ? loop : control);
        channel_config_set_irq_quiet(&dc, !interrupt);
        return DmaBlock{uint32_t(uintptr_t(samples)), uint32_t(uintptr_t(&pio->txf[sm])),
                        count, channel_config_get_ctrl_value(&dc)};
    };
    idle_plan[0] = descriptor(wave_hold, 2048, true, false);
    // Match the proven FPGA refresh: gap, init0, upload, 32768 hold clocks,
    // gap, init1, upload. The old image remains stored while USB receives.
    unsigned n = 0;
#ifdef PICO_PANEL_CHAIN_TEST
    // Static test uploads once at boot. Keep a valid unused update descriptor.
    update_plan[n++] = descriptor(wave_hold,2048,true,true);
#else
#if PICO_PANEL_REFRESH_CONTINUOUS
    // Keep A/B/C, OE and SCLK continuous; overlay all three original LAT commands.
    update_plan[n++] = descriptor(wave_continuous_frame_start, 2048, false, true);
#else
#if !PICO_PANEL_REFRESH_NO_GAP
    update_plan[n++] = descriptor(wave_gap, 625, false, false);
#endif
#if PICO_PANEL_REFRESH_NO_CONFIG
    update_plan[n++] = descriptor(wave_refresh_no_config, sizeof(wave_refresh_no_config) / 2, false, true);
#else
    update_plan[n++] = descriptor(wave_init_0, 1414, false, true);
#endif
#endif
    update_plan[n++] = descriptor(wave_visible, video::WAVE_SAMPLES, false, false);
    for (unsigned i = 0; i < 32; ++i)
        update_plan[n++] = descriptor(wave_hold, 2048, false, false);
#if PICO_PANEL_REFRESH_CONTINUOUS
    update_plan[n++] = descriptor(wave_continuous_frame_start, 2048, false, false);
#else
#if !PICO_PANEL_REFRESH_NO_GAP
    update_plan[n++] = descriptor(wave_gap, 625, false, false);
#endif
#if PICO_PANEL_REFRESH_NO_CONFIG
    update_plan[n++] = descriptor(wave_refresh_no_config, sizeof(wave_refresh_no_config) / 2, false, false);
#else
    update_plan[n++] = descriptor(wave_init_1, 1414, false, false);
#endif
#endif
    update_plan[n++] = descriptor(wave_visible, video::WAVE_SAMPLES, true, true);
    hard_assert(n == ((PICO_PANEL_REFRESH_CONTINUOUS || PICO_PANEL_REFRESH_NO_GAP) ? 36 : 38));
#endif

    // Four-word descriptors update READ_ADDR/WRITE_ADDR/COUNT/CTRL_TRIG.
    dma_channel_config cc = dma_channel_get_default_config(control);
    channel_config_set_transfer_data_size(&cc, DMA_SIZE_32);
    channel_config_set_read_increment(&cc, true);
    channel_config_set_write_increment(&cc, true);
    channel_config_set_ring(&cc, true, 4);
    channel_config_set_high_priority(&cc, true);
    dma_channel_configure(control, &cc, &dma_hw->ch[data].read_addr, blocks, 4, false);

    // The hardware loop selects a complete descriptor plan at a scan boundary.
    next_plan = uint32_t(uintptr_t(idle_plan));
    dma_channel_config lc = dma_channel_get_default_config(loop);
    channel_config_set_transfer_data_size(&lc, DMA_SIZE_32);
    channel_config_set_read_increment(&lc, false);
    channel_config_set_write_increment(&lc, false);
    channel_config_set_high_priority(&lc, true);
    dma_channel_configure(loop, &lc, &dma_hw->ch[control].al3_read_addr_trig, &next_plan, 1, false);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_complete);
    dma_channel_set_irq0_enabled(data, true);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_start_channel_mask(1u << control);
    while (!pio_sm_is_tx_fifo_full(pio, sm)) tight_loop_contents();
    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio_sm_set_enabled(pio, sm, true);
#ifdef PICO_PANEL_SOLAR
    solar_sm=sm;solar_offset=offset;solar_control=control;solar_loop=loop;
    ota::pause_hook=panel_pause;
    solar_app::init();
    bool displayed=false;
    uint64_t next_render=0;
#endif
    uint32_t stalls = 0, errors = 0, committed = 0;
    unsigned front = 0, rx_state = 0, position = 0;
    uint8_t command = 0, sequence = 0;
    uint16_t crc = 0xffff, received_crc = 0;
    uint64_t last_byte = 0;
    char usb_bytes[256];
    int usb_count = 0, usb_offset = 0;
    auto reply = [&](const char *status) {
        printf("PICO_VIDEO %s seq=%u crc=%04x frames=%lu txstall=%lu dma_error=%08lx\n",
               status, sequence, crc, (unsigned long)committed,
               (unsigned long)stalls, (unsigned long)errors);
    };
#ifdef PICO_PANEL_CHAIN_ARROW
    bool arrow_running=false,arrow_waiting=false;
    int arrow_left=14;
    unsigned arrow_pass=0;
    uint64_t arrow_due=0;
#endif
    while (true) {
        bool stalled = (pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm))) != 0;
#ifdef PICO_PANEL_SOLAR
        if(solar_paused)stalled=false;
#endif
        if (stalled) {
            ++stalls;
            pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
        }
        errors |= dma_hw->ch[data].ctrl_trig & (DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS |
                           DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS);
#ifdef PICO_DEFAULT_LED_PIN
        gpio_put(LED_PIN, stalls || errors || ((time_us_64() / 500000) & 1));
#endif
        if (phase == COMPLETE) {
            // Completion is DMA-to-FIFO; allow the final queued GPIO samples out.
            sleep_us(10);
            front ^= 1;
            ++committed;
            phase = IDLE;
#ifndef PICO_PANEL_SOLAR
            reply("ACK");
#endif
        }
#ifdef PICO_PANEL_SOLAR
        // Polling owns lwIP on this core; PIO/DMA continues even while Wi-Fi runs.
        solar_app::poll(stalls,errors);
#ifdef PICO_PANEL_CHAIN_ARROW
        if(!solar_paused && !ota::active && ota::confirmed && phase==IDLE) {
            uint64_t now=time_us_64();
            if(chain_arrow::requested) {
                chain_arrow::requested=false;arrow_running=true;arrow_waiting=false;
                arrow_left=14;arrow_pass=0;arrow_due=now;
            }
            if(arrow_running && arrow_waiting) {
                arrow_waiting=false;
                if(++arrow_pass==2) {
                    arrow_pass=0;
                    if(arrow_left>=256)arrow_running=false;
                    else {
                        arrow_due=now+(arrow_left==14?2500000:10000);
                        arrow_left+=8;
                    }
                }
            }
            if(arrow_running && now>=arrow_due) {
                // Startup is finished; only the idle waveform is being read now.
                blocks[0]=descriptor(wave_prefix,sizeof(wave_prefix)/2,false,true);
                blocks[1]=descriptor(wave_tail,sizeof(wave_tail)/2,false,false);
                for(unsigned word=0;word<4096;++word)
                    blocks[word+2]=descriptor(chain_arrow::samples[word%64][chain_arrow::mask(word,arrow_left)],32,false,false);
                blocks[4098]=descriptor(wave_hold,65536,true,true);
                dma_channel_config hold=dc;
                channel_config_set_ring(&hold,false,12);
                blocks[4098].control=channel_config_get_ctrl_value(&hold);
                arrow_waiting=true;phase=QUEUED;__dmb();next_plan=uint32_t(uintptr_t(blocks));
            }
        }
#endif
#ifndef PICO_PANEL_CHAIN_TEST
        if(solar_redraw){displayed=false;solar_redraw=false;}
        if(!solar_paused && phase==IDLE && time_us_64()>=next_render) {
            next_render=time_us_64()+250000;
            solar_app::frame(frames[front^1]);
            if(!displayed || memcmp(frames[front],frames[front^1],video::FRAME_BYTES)) {
                video::render(frames[front^1],wave_visible);
                phase=QUEUED; __dmb(); next_plan=uint32_t(uintptr_t(update_plan));
                displayed=true;
            }
        }
#endif
        tight_loop_contents();
        continue;
#endif
        if (phase != IDLE) { tight_loop_contents(); continue; }
        if (rx_state && time_us_64() - last_byte > 1000000) {
            rx_state = 0;
            reply("TIMEOUT");
        }
        if (usb_offset == usb_count) {
            usb_count = stdio_usb.in_chars(usb_bytes, sizeof(usb_bytes));
            usb_offset = 0;
            if (usb_count <= 0) { usb_count = 0; tight_loop_contents(); continue; }
        }
        uint8_t value = uint8_t(usb_bytes[usb_offset++]);
        last_byte = time_us_64();
        switch (rx_state) {
        case 0: if (value == 'P') rx_state = 1; break;
        case 1: rx_state = value == 'V' ? 2 : (value == 'P' ? 1 : 0); break;
        case 2:
            command = value;
            crc = video::crc_byte(0xffff, value);
            rx_state = (command == 1 || command == 2) ? 3 : 0;
            break;
        case 3:
            sequence = value;
            crc = video::crc_byte(crc, value);
            position = 0;
            rx_state = command == 1 ? 4 : 5;
            break;
        case 4:
            frames[front ^ 1][position++] = value;
            crc = video::crc_byte(crc, value);
            if (position == video::FRAME_BYTES) rx_state = 5;
            break;
        case 5: received_crc = uint16_t(value) << 8; rx_state = 6; break;
        case 6:
            received_crc |= value;
            rx_state = 0;
            if (received_crc != crc) reply("CRC_ERROR");
            else if (command == 2) reply("STATUS");
            else {
                // No DMA reader uses wave_visible while the idle plan scans.
#ifndef PICO_PANEL_CHAIN_TEST
                video::render(frames[front ^ 1], wave_visible);
#endif
                phase = QUEUED;
                __dmb();
                next_plan = uint32_t(uintptr_t(update_plan));
            }
            break;
        }
    }
}
