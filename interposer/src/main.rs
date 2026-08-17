#![no_std]
#![no_main]

use defmt::info;
use defmt_rtt as _;
use embassy_executor::Spawner;
use core::sync::atomic::{AtomicU8, Ordering};

use embassy_futures::select::{select, Either};

use embassy_stm32::Peri;
use embassy_stm32::gpio::{AfType, Flex, Level, Output, OutputType};
// rename Speed to avoid clash with USB Speed:
use embassy_stm32::gpio::Speed as GpioSpeed;
use embassy_stm32::gpio::{Input, Pull};
use embassy_stm32::{interrupt, pac};
use embassy_stm32::bind_interrupts;
use embassy_stm32::interrupt::typelevel::Handler;
use embassy_stm32::interrupt::typelevel::Interrupt;
use embassy_stm32::rcc::{
    AHBPrescaler, APBPrescaler, Hse, HseMode, Pll, PllMul, PllPDiv,
    PllPreDiv, PllQDiv, PllSource, Sysclk,
};
use embassy_stm32::time::Hertz;
use embassy_stm32::Config;

use embassy_stm32::usb::InterruptHandler as UsbInterruptHandler;
use embassy_stm32::usb::Driver as UsbDriver;

use embassy_usb::class::hid::{
    HidWriter,
    HidSubclass,
    HidBootProtocol,
    Config as HidConfig,
    State as HidState,
};

use embassy_usb_synopsys_otg::{PhyType, otg_v1::Otg};
use embassy_usb_synopsys_otg::host::{
    HostState,
    OtgHost,
    OtgHostInstance,
    on_host_interrupt,
};
use embassy_usb_synopsys_otg::host::OtgHostAllocator;

use embassy_usb_host::{BusState, BusRoute, bus};
use embassy_usb_host::class::kbd::{KbdEvent, KbdHandler};
use embassy_usb_host::handler::HandlerEvent;
use embassy_usb_host::{BusController, BusHandle};

use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

use static_cell::StaticCell;

use embassy_time::Timer;
use panic_probe as _;

defmt::timestamp!("{=u64:us}", embassy_time::Instant::now().as_micros());

type HsDriver = UsbDriver<'static, embassy_stm32::peripherals::USB_OTG_HS>;

static HW_VERSION: AtomicU8 = AtomicU8::new(0);

#[derive(Clone, Copy)]
enum LedCmd {
    KeyboardConnected,
    KeyboardDisconnected,
    NotAKeyboard,
}

static LED_CMD: Signal<CriticalSectionRawMutex, LedCmd> = Signal::new();

static HOST_STATE: HostState<8> = HostState::new();
static BUS_STATE: BusState = BusState::new();

static EP_OUT_BUF:      StaticCell<[u8; 256]>   = StaticCell::new();
static DEV_DESC:        StaticCell<[u8; 256]>   = StaticCell::new();
static CONF_DESC:       StaticCell<[u8; 256]>   = StaticCell::new();
static BOS_DESC:        StaticCell<[u8; 64]>    = StaticCell::new();
static MSOS_DESC:       StaticCell<[u8; 64]>    = StaticCell::new();
static HID_STATE:       StaticCell<HidState>    = StaticCell::new();

static HID_KEYBOARD_REPORT_DESC: &[u8] = &[
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224) — modifier keys
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1 bit)
    0x95, 0x08,  //   Report Count (8) — 8 modifier bits
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8 bits) — reserved byte
    0x81, 0x01,  //   Input (Constant)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8 bits) — 6 keycodes
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0,        // End Collection
];

struct OtgFsHostIrq;

impl Handler<interrupt::typelevel::OTG_FS> for OtgFsHostIrq {
    unsafe fn on_interrupt() {
        let r = unsafe { Otg::from_ptr(pac::USB_OTG_FS.as_ptr() as *mut ()) };
        on_host_interrupt(r, &HOST_STATE, 8);
    }
}

bind_interrupts!(struct Irqs {
    OTG_FS => OtgFsHostIrq;
    OTG_HS => UsbInterruptHandler<embassy_stm32::peripherals::USB_OTG_HS>;
});

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    // 25 MHz HSE → SYSCLK=216 MHz, APB1=54 MHz, APB2=108 MHz, PLLQ=48 MHz (USB)
    let mut config = Config::default();
    config.rcc.hse = Some(Hse {
        freq: Hertz(25_000_000),
        mode: HseMode::Oscillator,
    });
    config.rcc.pll_src = PllSource::HSE;
    config.rcc.pll = Some(Pll {
        prediv: PllPreDiv::DIV25,
        mul: PllMul::MUL432,
        divp: Some(PllPDiv::DIV2), // 216 MHz SYSCLK
        divq: Some(PllQDiv::DIV9), // 48 MHz USB
        divr: None,
    });
    config.rcc.sys = Sysclk::PLL1_P;
    config.rcc.ahb_pre = AHBPrescaler::DIV1;
    config.rcc.apb1_pre = APBPrescaler::DIV4; // 54 MHz
    config.rcc.apb2_pre = APBPrescaler::DIV2; // 108 MHz
    // voltage scale / overdrive handled automatically by embassy-stm32 for F7

    let p = embassy_stm32::init(config);

    let version = hw_version(p.PD4, p.PD5, p.PD6, p.PD7);
    HW_VERSION.store(version, Ordering::Relaxed);
    info!("Interposer version: {}", version + 1);

    let mut usb_dev_config = embassy_usb::Config::new(0x0483, 0x5741);
    usb_dev_config.manufacturer = Some("Acme Electronics");
    usb_dev_config.product = Some("Interposer");
    usb_dev_config.serial_number = Some("0001");
    usb_dev_config.max_power = 100;
    usb_dev_config.max_packet_size_0 = 64;

    let mut usb_config = embassy_stm32::usb::Config::default();
    usb_config.vbus_detection = false;

    let driver = UsbDriver::new_hs(
        p.USB_OTG_HS, Irqs,
        p.PB15, p.PB14, // DP, DM
        EP_OUT_BUF.init([0u8; 256]),
        usb_config,
    );

    let mut builder = embassy_usb::Builder::new(
        driver,
        usb_dev_config,
        DEV_DESC.init([0u8; 256]),
        CONF_DESC.init([0u8; 256]),
        BOS_DESC.init([0u8; 64]),
        MSOS_DESC.init([0u8; 64]),
    );

    let hid_config = HidConfig {
        report_descriptor: HID_KEYBOARD_REPORT_DESC,
        request_handler: None,
        poll_ms: 10,
        max_packet_size: 8,
        hid_subclass: HidSubclass::Boot,
        hid_boot_protocol: HidBootProtocol::Keyboard,
    };
    let hid_state = HID_STATE.init(HidState::new());
    let hid_writer = HidWriter::<_, 8>::new(&mut builder, hid_state, hid_config);
    let usb_device = builder.build();

    let _dm = {
        let mut pin = Flex::new(p.PA11);
        pin.set_as_af_unchecked(10, AfType::output(OutputType::PushPull,
                                                   GpioSpeed::VeryHigh));
        pin
    };

    let _dp = {
        let mut pin = Flex::new(p.PA12);
        pin.set_as_af_unchecked(10, AfType::output(OutputType::PushPull,
                                                   GpioSpeed::VeryHigh));
        pin
    };

    pac::RCC.ahb2enr().modify(|w| w.set_usb_otg_fsen(true));

    let instance = OtgHostInstance {
        regs: unsafe { Otg::from_ptr(pac::USB_OTG_FS.as_ptr() as *mut ()) },
        state: &HOST_STATE,
        fifo_depth_words: 320,
        channel_count: 8,
        phy_type: PhyType::InternalFullSpeed,
    };

    let otg_host = OtgHost::new(instance);
    unsafe { interrupt::typelevel::OTG_FS::enable(); }
    let (controller, handle) = bus(otg_host, &BUS_STATE);

    // needed even for internal OTGPHYC
    pac::RCC.ahb1enr().modify(|w| w.set_usb_otg_hsulpien(true));
    // OTGPHYC clock for OTG_HS HS PHY
    pac::RCC.apb2enr().modify(|w| w.set_usbphycen(true));
    unsafe { init_otgphyc(); }
    spawner.spawn(usb_task(usb_device).unwrap());

    spawner.spawn(led_task(p.PE9, p.PE11, p.PE13).unwrap());
    spawner.spawn(kbd_task(controller, handle, p.PB0, hid_writer).unwrap());

    // All LEDs off at init (active high; RGB red has enough leakage to glow if floating)
    let _rgb_r = Output::new(p.PB4,  Level::Low , GpioSpeed::Low); // RGB RED
    let _rgb_g = Output::new(p.PB5,  Level::Low , GpioSpeed::Low); // RGB GREEN
    let _rgb_b = Output::new(p.PB6,  Level::Low , GpioSpeed::Low); // RGB BLUE

    info!("STM32F723 Embassy bringup — SYSCLK=216MHz");

    loop {
        Timer::after_millis(1000).await;
    }
}

fn hw_version(
    pd4: Peri<'static, embassy_stm32::peripherals::PD4>,
    pd5: Peri<'static, embassy_stm32::peripherals::PD5>,
    pd6: Peri<'static, embassy_stm32::peripherals::PD6>,
    pd7: Peri<'static, embassy_stm32::peripherals::PD7>,
) -> u8 {
    Input::new(pd4, Pull::Up).is_high() as u8 |
       ((Input::new(pd5, Pull::Up).is_high() as u8) << 1) |
       ((Input::new(pd6, Pull::Up).is_high() as u8) << 2) |
       ((Input::new(pd7, Pull::Up).is_high() as u8) << 3)
}
    

#[embassy_executor::task]
async fn kbd_task(
    mut controller: BusController<'static, OtgHost<'static, 8>>,
    handle: BusHandle<'static, OtgHostAllocator<'static, 8>>,
    vbus_ena: Peri<'static, embassy_stm32::peripherals::PB0>,
    mut hid: HidWriter<'static, HsDriver, 8>,
) {
    // Enable USB-A VBUS
    let _usba_en = Output::new(vbus_ena, Level::High, GpioSpeed::Low);

    loop {
        LED_CMD.signal(LedCmd::KeyboardDisconnected);
        // wait for a keyboard to be plugged in
        let speed = controller.wait_for_connection().await;
        info!("USB device connected: {:?}", speed);

        // enumerate the device
        let mut config_buf = [0u8; 256];
        let (enum_info, _) = match handle.enumerate(BusRoute::Direct(speed),
                                                    &mut config_buf).await {
            Ok(v) => v,
            Err(e) => {
                info!("Enumeration failed: {:?}", e);
                continue;
            }
        };

        // try to claim it as a boot keyboard
        let mut kbd = match KbdHandler::try_register(&handle, &enum_info).await {
            Ok(k) => k,
            Err(e) => {
                LED_CMD.signal(LedCmd::NotAKeyboard);
                info!("Not a keyboard: {:?}", e);
                handle.free_address(enum_info.device_address);
                continue;
            }
        };

        info!("Keyboard ready");
        LED_CMD.signal(LedCmd::KeyboardConnected);

        // read events until disconnect
        loop {
            match select(
                controller.wait_for_device_event(),
                kbd.wait_for_event(),
            ).await {
                Either::First(_) => {
                    // port event (disconnect)
                    info!("Keyboard disconnected");
                    handle.free_address(enum_info.device_address);
                    LED_CMD.signal(LedCmd::KeyboardDisconnected);
                    break;
                }
                Either::Second(Ok(HandlerEvent::HandlerEvent(
                            KbdEvent::KeyStatusUpdate(update)))) => {
                    info!("mod={:08b} keys={:?}",
                          update.modifiers, update.keypress);
                    let mut report = [0u8; 8];
                    report[0] = update.modifiers;
                    for (i, k) in update.keypress.iter().enumerate() {
                        report[2 + i] = k.map_or(0, |v| v.get());
                    }
                    let _ = hid.write(&report).await;
                }
                Either::Second(Ok(_)) => {}
                Either::Second(Err(e)) => {
                    info!("Keyboard error: {:?}", e);
                    handle.free_address(enum_info.device_address);
                    LED_CMD.signal(LedCmd::KeyboardDisconnected);
                    break;
                }
            }
        }
    }
}

unsafe fn init_otgphyc() {
    const PHYC: *mut u32 = 0x4001_7C00 as *mut u32;
    // offsets in u32 words: PLL=0x00, TUNE=0x0C, LDO=0x18
    let pll  = PHYC.add(0x00 / 4);
    let tune = PHYC.add(0x0C / 4);
    let ldo  = PHYC.add(0x18 / 4);

    // Enable LDO (set bit 2), wait for LDO ready (bit 1)
    ldo.write_volatile(ldo.read_volatile() | (1 << 2));
    while ldo.read_volatile() & (1 << 1) == 0 {}

    // Select 25 MHz reference: PLLSEL=5 → bits [3:1] = 0b101
    pll.write_volatile(0x5 << 1);

    // Apply tuning value (from STM32F7 HAL: USB_HS_PHYC_TUNE_VALUE)
    tune.write_volatile(tune.read_volatile() | 0x0000_0F13);

    // Enable PLL (bit 0)
    pll.write_volatile(pll.read_volatile() | 1);

    // 2ms settle time — embassy_time::block_for is fine here (pre-scheduler)
    embassy_time::block_for(embassy_time::Duration::from_millis(2));
}

#[embassy_executor::task]
async fn usb_task(mut device: embassy_usb::UsbDevice<'static, HsDriver>) {
    info!("USB device task started");
    device.run().await;
}

#[embassy_executor::task]
async fn led_task (
    pin_b: Peri<'static, embassy_stm32::peripherals::PE9>,
    pin_g: Peri<'static, embassy_stm32::peripherals::PE11>,
    pin_y: Peri<'static, embassy_stm32::peripherals::PE13>,
) {
    let _led_b     = Output::new(pin_b,  Level::High, GpioSpeed::Low); // BLUE
    let _led_g     = Output::new(pin_g, Level::Low , GpioSpeed::Low); // GREEN
    let mut led_y  = Output::new(pin_y, Level::Low , GpioSpeed::Low); // YELLOW

    let mut yellow = 0u8;   // 0 = steady, nonzero = flashing

    loop {
        match select(LED_CMD.wait(), Timer::after_millis(500)).await {
            Either::First(cmd) => {
                yellow = 0;     // clear flashing state on any new command
                match cmd {
                    LedCmd::KeyboardConnected    => led_y.set_high(),
                    LedCmd::KeyboardDisconnected => led_y.set_low(),
                    LedCmd::NotAKeyboard         => { yellow = 1; },
                }
            }
            Either::Second(_) => {
                if yellow != 0 {
                    led_y.toggle();
                }
            }
        }
    }
}
