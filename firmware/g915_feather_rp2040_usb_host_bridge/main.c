#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "tusb.h"
#include "device/dcd.h"

#include "app_config.h"
#include "core/report_pipe.h"
#include "host/sof_phase_lock.h"
#include "host/usb_host_keyboard.h"
#include "usb/usb_keyboard.h"

static report_pipe_t report_pipe;
static bool led_state;

//--------------------------------------------------------------------+
// Console SOF observation (core 0, interrupt context)
//--------------------------------------------------------------------+

// TinyUSB invokes this hook from inside the USB device interrupt, as it queues
// each event for tud_task(). That is the earliest and least jittery place to
// timestamp the console's SOF for the frame phase lock. The SOF frame number
// register is updated by hardware before the interrupt fires.
void __not_in_flash_func(tud_event_hook_cb)(uint8_t rhport, uint32_t eventid,
                                            bool in_isr) {
    (void)rhport;
    (void)in_isr;
    switch (eventid) {
        case DCD_EVENT_SOF:
            sof_phase_lock_observe_console_sof(
                time_us_32(), (uint16_t)(usb_hw->sof_rd & USB_SOF_RD_BITS));
            break;
        case DCD_EVENT_BUS_RESET:
        case DCD_EVENT_UNPLUGGED:
        case DCD_EVENT_SUSPEND:
            sof_phase_lock_forget_console();
            break;
        default:
            break;
    }
}

//--------------------------------------------------------------------+
// USB CDC diagnostics
//--------------------------------------------------------------------+

#if BRIDGE_USB_CDC_DIAGNOSTICS
#define DIAGNOSTIC_LOG_QUEUE_DEPTH 64u
#define DIAGNOSTIC_LOG_MESSAGE_SIZE 128u
#define USB_FRAME_PERIOD_US 1000

typedef struct {
    char text[DIAGNOSTIC_LOG_MESSAGE_SIZE];
} diagnostic_log_message_t;

static queue_t diagnostic_log_queue;

// Where within the console's frame its IN token for our endpoint arrives,
// measured from the console SOF the phase lock tracks. Completion is observed
// from task context and so is always a little late; the estimate snaps to
// earlier samples and follows later ones slowly, like the SOF tracker.
static uint32_t token_offset_us;
static uint32_t token_offset_samples;

static void diagnostic_log_init(void) {
    queue_init(&diagnostic_log_queue, sizeof(diagnostic_log_message_t),
               DIAGNOSTIC_LOG_QUEUE_DEPTH);
}

int bridge_debug_printf(const char *format, ...) {
    diagnostic_log_message_t message = {0};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(message.text, sizeof(message.text), format, args);
    va_end(args);

    if (!queue_try_add(&diagnostic_log_queue, &message)) {
        diagnostic_log_message_t discarded;
        (void)queue_try_remove(&diagnostic_log_queue, &discarded);
        (void)queue_try_add(&diagnostic_log_queue, &message);
    }
    return length;
}

static void observe_delivery(const usb_keyboard_delivery_t *delivery,
                             void *context) {
    (void)context;
    sof_phase_lock_status_t phase;
    sof_phase_lock_get_status(&phase);
    if (!phase.reference_valid) return;

    int32_t offset =
        (int32_t)(delivery->completed_us - phase.reference_us) % USB_FRAME_PERIOD_US;
    if (offset < 0) offset += USB_FRAME_PERIOD_US;

    if (token_offset_samples == 0 || (uint32_t)offset < token_offset_us) {
        token_offset_us = (uint32_t)offset;
    } else {
        token_offset_us += ((uint32_t)offset - token_offset_us) / 16u;
    }
    ++token_offset_samples;
}

static void write_line(const char *line, int length) {
    if (length > 0) tud_cdc_write(line, (uint32_t)length);
}

static void service_usb_diagnostics(void) {
    static uint32_t last_report_ms;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!tud_cdc_connected()) return;

    diagnostic_log_message_t message;
    while (queue_try_remove(&diagnostic_log_queue, &message)) {
        tud_cdc_write(message.text, (uint32_t)strnlen(
            message.text, sizeof(message.text)));
    }

    if ((uint32_t)(now - last_report_ms) < 1000u) {
        tud_cdc_write_flush();
        return;
    }
    last_report_ms = now;

    usb_host_keyboard_diagnostics_t diagnostics;
    usb_host_keyboard_get_diagnostics(&diagnostics);
    usb_keyboard_latency_t latency;
    usb_keyboard_get_latency(&latency);
    sof_phase_lock_status_t phase;
    sof_phase_lock_get_status(&phase);

    const unsigned n = latency.count;
    const unsigned average_us = n ? (unsigned)(latency.total_us / n) : 0u;
    const unsigned arm_average_us = n ? (unsigned)(latency.arm_total_us / n) : 0u;
    const unsigned poll_average_us = n ? (unsigned)(latency.poll_total_us / n) : 0u;

    char line[512];
    int length = snprintf(
        line, sizeof(line),
        "host_stage=%u vid=%04x pid=%04x hid_interfaces=%u keyboards=%u "
        "binterval=%u poll_interval=%u poll_frames=%u arm_failures=%u\r\n",
        (unsigned)diagnostics.status, diagnostics.vid, diagnostics.pid,
        diagnostics.hid_interface_count, diagnostics.keyboard_interface_count,
        diagnostics.declared_poll_interval, diagnostics.poll_interval,
        (unsigned)diagnostics.min_report_frame_gap,
        (unsigned)diagnostics.report_arm_failure_count);
    write_line(line, length);

    length = snprintf(
        line, sizeof(line),
        "latency_us n=%u min=%u avg=%u max=%u arm_avg=%u arm_max=%u "
        "poll_avg=%u poll_max=%u frames_waited=%u/%u/%u/%u\r\n",
        n, (unsigned)latency.min_us, average_us, (unsigned)latency.max_us,
        arm_average_us, (unsigned)latency.arm_max_us, poll_average_us,
        (unsigned)latency.poll_max_us, (unsigned)latency.frames_waited[0],
        (unsigned)latency.frames_waited[1], (unsigned)latency.frames_waited[2],
        (unsigned)latency.frames_waited[3]);
    write_line(line, length);

    length = snprintf(
        line, sizeof(line),
        "phase steering=%u ref=%u locked=%u err_us=%d adjust=%u missed=%u "
        "sof_n=%u token_off_us=%u token_n=%u\r\n",
        (unsigned)phase.steering_enabled, (unsigned)phase.reference_valid,
        (unsigned)phase.locked, (int)phase.error_us,
        (unsigned)phase.adjustment_count, (unsigned)phase.missed_frames,
        (unsigned)phase.sof_observations, (unsigned)token_offset_us,
        (unsigned)token_offset_samples);
    write_line(line, length);

    int position = snprintf(line, sizeof(line), "hist_%uus=",
                            (unsigned)USB_KEYBOARD_LATENCY_BUCKET_US);
    for (unsigned i = 0;
         i < USB_KEYBOARD_LATENCY_BUCKET_COUNT && position > 0 &&
         (size_t)position < sizeof(line);
         ++i) {
        const int written = snprintf(
            line + position, sizeof(line) - (size_t)position, "%u%s",
            (unsigned)latency.histogram[i],
            i + 1u < USB_KEYBOARD_LATENCY_BUCKET_COUNT ? " " : "\r\n");
        if (written < 0) break;
        position += written;
    }
    if (position > 0) {
        if ((size_t)position > sizeof(line)) position = (int)sizeof(line);
        write_line(line, position);
    }
    tud_cdc_write_flush();
}
#endif

#if APP_SOF_PHASE_LEAD_US > 0 || BRIDGE_USB_CDC_DIAGNOSTICS
// Deliver the console's SOF to tud_event_hook_cb() so the frame phase lock can
// track it. A registration made once at start-up does not survive the bus
// reset the console issues at the start of every enumeration: TinyUSB's reset
// handling drops the registration and the controller's interrupt enable with
// it. Renew the subscription whenever the device becomes configured. Turning
// it off first forces TinyUSB to re-enable the SOF interrupt even if it still
// believes the registration to be in force. This also queues one SOF event
// per millisecond for tud_task(), which is cheap.
static void service_console_sof_subscription(void) {
    static bool subscribed;
    const bool mounted = tud_mounted();
    if (mounted && !subscribed) {
        tud_sof_cb_enable(false);
        tud_sof_cb_enable(true);
        subscribed = true;
    } else if (!mounted) {
        subscribed = false;
    }
}
#endif

static void service_status_led(void) {
    bool wanted = usb_host_keyboard_status() == USB_HOST_KEYBOARD_READY;
    if (!wanted) {
        const uint64_t elapsed_ms = to_ms_since_boot(get_absolute_time());
        wanted = ((elapsed_ms / APP_STATUS_LED_BLINK_INTERVAL_MS) & 1u) != 0;
    }

    if (wanted == led_state) return;
    led_state = wanted;
    gpio_put(PICO_DEFAULT_LED_PIN, wanted);
}

int main(void) {
    // Pico-PIO-USB requires an integer multiple of 12 MHz. 120 MHz is its
    // documented RP2040 operating clock and remains valid for native USB.
    if (!set_sys_clock_khz(120000, true)) {
        panic("Could not set the 120 MHz PIO USB clock");
    }

#if BRIDGE_USB_CDC_DIAGNOSTICS
    diagnostic_log_init();
#endif
    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 0);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, false);

    gpio_init(APP_USB_HOST_5V_ENABLE_PIN);
    gpio_set_dir(APP_USB_HOST_5V_ENABLE_PIN, GPIO_OUT);
    // Force every receiver through a real disconnect before host startup.
    gpio_put(APP_USB_HOST_5V_ENABLE_PIN, false);
    sleep_ms(20);
    gpio_put(APP_USB_HOST_5V_ENABLE_PIN, true);
    sleep_ms(100);

    report_pipe_init(&report_pipe);
    usb_keyboard_init(&report_pipe);
    usb_host_keyboard_init();
#if BRIDGE_USB_CDC_DIAGNOSTICS
    usb_keyboard_set_delivery_observer(observe_delivery, NULL);
#endif

    const tusb_rhport_init_t device_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(APP_USB_DEVICE_RHPORT, &device_init)) {
        panic("TinyUSB device initialization failed");
    }

    multicore_launch_core1(usb_host_keyboard_core1);
    printf("G915 Feather RP2040 USB Host bridge started\n");

    while (true) {
        tud_task();
#if APP_SOF_PHASE_LEAD_US > 0 || BRIDGE_USB_CDC_DIAGNOSTICS
        service_console_sof_subscription();
#endif
        usb_host_keyboard_task(&report_pipe);
        usb_keyboard_task();
#if BRIDGE_USB_CDC_DIAGNOSTICS
        service_usb_diagnostics();
#endif
        service_status_led();
        tight_loop_contents();
    }
}
