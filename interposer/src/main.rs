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
use embassy_stm32::sdmmc::{Sdmmc, Config as SdmmcConfig};
use embassy_stm32::sdmmc::sd::{StorageDevice, CmdBlock, Addressable};

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
use embassy_sync::channel::Channel;

use static_cell::StaticCell;

use embassy_time::Timer;

use block_device_driver::{BlockDevice};

use mipidsi::interface::SpiInterface;   // Provides builder for DisplayInterface
use mipidsi::{Builder,                  // Provides builder for Display
    options::ColorOrder,
    models::ST7735s
};
use embedded_graphics::{prelude::*, pixelcolor::Rgb565};    // Color type
use embedded_graphics::text::Text;
use embedded_graphics::{mono_font::{ ascii:: FONT_5X7}};
use embedded_graphics::{mono_font::{ ascii:: FONT_7X14}};
use embedded_graphics::{mono_font::MonoTextStyle};
use embedded_graphics::text::{Baseline, TextStyleBuilder};

use heapless::String;
use core::fmt::Write;

use aligned::{Aligned, A4};

use panic_probe as _;

mod msc;
static MSC: StaticCell<msc::Msc<'static, HsDriver>> = StaticCell::new();

defmt::timestamp!("{=u64:us}", embassy_time::Instant::now().as_micros());

type HsDriver = UsbDriver<'static, embassy_stm32::peripherals::USB_OTG_HS>;

static HW_VERSION: AtomicU8 = AtomicU8::new(0);

enum DisplayCmd {
    WriteLine(u8, heapless::String<26>),    // row, text
    Clear,
}

static DISPLAY_CHANNEL: Channel<CriticalSectionRawMutex, DisplayCmd, 4> =
        Channel::new();

#[derive(Clone, Copy)]
enum LedCmd {
    KeyboardConnected,
    KeyboardDisconnected,
    NotAKeyboard,
}

static LED_CMD: Signal<CriticalSectionRawMutex, LedCmd> = Signal::new();

static HOST_STATE: HostState<8> = HostState::new();
static BUS_STATE: BusState = BusState::new();

static EP_OUT_BUF:      StaticCell<[u8; 1024]>  = StaticCell::new();
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
    SDMMC1 => embassy_stm32::sdmmc::InterruptHandler<
            embassy_stm32::peripherals::SDMMC1>;
    DMA2_STREAM3 => embassy_stm32::dma::InterruptHandler<
            embassy_stm32::peripherals::DMA2_CH3>;
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

    // All LEDs off at init (active high; RGB red has enough leakage to glow if floating)
    let _rgb_r = Output::new(p.PB4,  Level::Low , GpioSpeed::Low); // RGB RED
    let _rgb_g = Output::new(p.PB5,  Level::Low , GpioSpeed::Low); // RGB GREEN
    let _rgb_b = Output::new(p.PB6,  Level::Low , GpioSpeed::Low); // RGB BLUE

    info!("STM32F723 Embassy bringup — SYSCLK=216MHz");

    let version = hw_version(p.PD4, p.PD5, p.PD6, p.PD7) + 1;
    HW_VERSION.store(version, Ordering::Relaxed);
    info!("Interposer version: {}", version);

    let mut usb_dev_config = embassy_usb::Config::new(0x0483, 0x5741);
    usb_dev_config.manufacturer = Some("Acme Electronics");
    usb_dev_config.product = Some("Interposer");
    usb_dev_config.serial_number = Some("0001");
    usb_dev_config.max_power = 100;
    usb_dev_config.max_packet_size_0 = 64;

    let mut usb_config = embassy_stm32::usb::Config::default();
    usb_config.vbus_detection = false;

    info!("creating USB driver");
    let driver = UsbDriver::new_hs(
        p.USB_OTG_HS, Irqs,
        p.PB15, p.PB14, // DP, DM
        EP_OUT_BUF.init([0u8; 1024]),
        usb_config,
    );

    info!("creating builder");
    let mut builder = embassy_usb::Builder::new(
        driver,
        usb_dev_config,
        DEV_DESC.init([0u8; 256]),
        CONF_DESC.init([0u8; 256]),
        BOS_DESC.init([0u8; 64]),
        MSOS_DESC.init([0u8; 64]),
    );

    info!("creating MSC");
    let msc = msc::Msc::new(&mut builder);


    let hid_config = HidConfig {
        report_descriptor: HID_KEYBOARD_REPORT_DESC,
        request_handler: None,
        poll_ms: 10,
        max_packet_size: 8,
        hid_subclass: HidSubclass::Boot,
        hid_boot_protocol: HidBootProtocol::Keyboard,
    };
    let hid_state = HID_STATE.init(HidState::new());
    info!("creating HID");
    let hid_writer = HidWriter::<_, 8>::new(&mut builder, hid_state, hid_config);
    info!("building USB Device");
    let usb_device = builder.build();
    info!("spawning tasks");

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
    info!("spawning tasks");
    spawner.spawn(usb_task(usb_device).unwrap());
    info!("usb spawned");

    spawner.spawn(led_task(p.PE9, p.PE11, p.PE13).unwrap());
    info!("led spawned");
    spawner.spawn(display_task(p.SPI1, p.PA5, p.PA7, p.PA4, p.PB1, p.PA1,
                               p.PA15).unwrap());
    info!("display spawned");
    spawner.spawn(kbd_task(controller, handle, p.PB0, hid_writer).unwrap());
    info!("keyboard spawned");

    spawner.spawn(sd_task(
            p.SDMMC1, p.DMA2_CH3,
            p.PC12, p.PD2,
            p.PC8, p.PC9, p.PC10, p.PC11,
    ).unwrap());
    info!("sd spawned");
    let msc = MSC.init(msc);
    spawner.spawn(msc_task(msc).unwrap());
    info!("msc spawned");

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

#[embassy_executor::task]
async fn sd_task(
    sdmmc:  Peri<'static, embassy_stm32::peripherals::SDMMC1>,
    dma:    Peri<'static, embassy_stm32::peripherals::DMA2_CH3>,
    clk:    Peri<'static, embassy_stm32::peripherals::PC12>,
    cmd:    Peri<'static, embassy_stm32::peripherals::PD2>,
    d0:     Peri<'static, embassy_stm32::peripherals::PC8>,
    _d1:     Peri<'static, embassy_stm32::peripherals::PC9>,
    _d2:     Peri<'static, embassy_stm32::peripherals::PC10>,
    _d3:     Peri<'static, embassy_stm32::peripherals::PC11>,
) {
    info!("SD task started");
    let mut buf: String<26> = String::new();

#[cfg(feature = "gpio-test")]
{
   // validate conectivity
   let mut pin_clk = Output::new(clk, Level::Low, GpioSpeed::VeryHigh);
   let mut pin_cmd = Output::new(cmd, Level::Low, GpioSpeed::VeryHigh);
   let mut pin_d0  = Output::new(d0,  Level::Low, GpioSpeed::VeryHigh);
   let mut pin_d1  = Output::new(d1,  Level::Low, GpioSpeed::VeryHigh);
   let mut pin_d2  = Output::new(d2,  Level::Low, GpioSpeed::VeryHigh);
   let mut pin_d3  = Output::new(d3,  Level::Low, GpioSpeed::VeryHigh);

   loop {
       pin_clk.toggle();
       pin_cmd.toggle();
       pin_d0.toggle();
       pin_d1.toggle();
       pin_d2.toggle();
       pin_d3.toggle();
       // embassy_time::Timer::after_secs(1).await;
       cortex_m::asm::delay(270); // ~1.25 μs at 216 MHz → ~400 kHz
   }
} // End of gpio-test
#[cfg(feature = "sdmmc_1bit")]
    let mut sd = Sdmmc::new_1bit(
        sdmmc, dma, Irqs, clk, cmd, d0,
        SdmmcConfig::default(),
    );
#[cfg(feature = "sdmmc_4bit")]
    let mut sd = Sdmmc::new_4bit(
        sdmmc, dma, Irqs, clk, cmd, d0, d1, d2, d3,
        SdmmcConfig::default(),
    );

    let mut cmd_block = CmdBlock::new();
    embassy_time::Timer::after_millis(500).await;
    loop {
        match StorageDevice::new_sd_card(&mut sd, &mut cmd_block,
                                         Hertz(400_000)).await {
            Ok(mut dev) => {
                info!("SD card: {}MB", dev.card().size() / 1_000_000);
                // 512 bytes, one SD block
                buf.clear();
                if dev.card().size() > 1_000_000_000 {
                    write!(&mut buf, "SD card: {}GB", dev.card().size() /
                           1_000_000_000).unwrap();
                } else {
                    write!(&mut buf, "SD card: {}MB", dev.card().size() /
                           1_000_000).unwrap();
                }
                DISPLAY_CHANNEL.send(DisplayCmd::WriteLine(28, buf)).await;
                let mut buf: Aligned<A4, [u8; 512]> = Aligned([0u8; 512]);
                loop {
                    dev.read(0, core::slice::from_mut(&mut buf)).await.ok();
                    embassy_time::Timer::after_millis(1000).await;
                }
                //break;
            }
            Err(e) => {
                info!("SD init failed: {:?}", e);
                DISPLAY_CHANNEL.send(DisplayCmd::Clear).await;
                embassy_time::Timer::after_millis(1000).await;
            }
        }
    }
/*
    loop {
        embassy_time::Timer::after_secs(60).await;
    }
*/
}

#[embassy_executor::task]
async fn display_task(
    spi1: Peri<'static, embassy_stm32::peripherals::SPI1>,
    pa5:  Peri<'static, embassy_stm32::peripherals::PA5>,   // SCK
    pa7:  Peri<'static, embassy_stm32::peripherals::PA7>,   // MOSI
    pa4:  Peri<'static, embassy_stm32::peripherals::PA4>,   // CS
    pb1:  Peri<'static, embassy_stm32::peripherals::PB1>,   // DC
    pa1:  Peri<'static, embassy_stm32::peripherals::PA1>,   // RST
    pa15: Peri<'static, embassy_stm32::peripherals::PA15>,  // backlight
) {
    info!("Display Task started");
    let mut spi_config = embassy_stm32::spi::Config::default();
    spi_config.frequency = Hertz(12_000_000);
    let spi = embassy_stm32::spi::Spi::new_blocking_txonly(spi1, pa5, pa7,
                                                           spi_config);
    let dc  = Output::new(pb1, Level::High, GpioSpeed::Low);
    let rst = Output::new(pa1, Level::Low,  GpioSpeed::Low);
    let cs  = Output::new(pa4, Level::High, GpioSpeed::High);
    let _bl = Output::new(pa15,Level::High, GpioSpeed::Low);
    let spi_dev = embedded_hal_bus::spi::ExclusiveDevice::
                new_no_delay(spi, cs).unwrap();

    let mut buffer = [0u8; 512];

    let di = SpiInterface::new(spi_dev, dc, &mut buffer);

    let mut display = Builder::new(ST7735s, di)
        .reset_pin(rst)
        .color_order(ColorOrder::Bgr)
        .display_size(128, 128)
        .display_offset(2, 1)
        .init(&mut embassy_time::Delay).unwrap();

    let mut buf: String<25> = String::new();

    display.clear(Rgb565::BLACK).unwrap();

    // Create a new character style
    let text_style = TextStyleBuilder::new().baseline(Baseline::Top).build();
    let header_style = MonoTextStyle::new(&FONT_7X14, Rgb565::WHITE);
    let body_style = MonoTextStyle::new(&FONT_5X7, Rgb565::WHITE);

    write!(&mut buf, "Interposer").unwrap();
    Text::with_text_style(&buf, Point::new(0,0), header_style, text_style)
        .draw(&mut display).unwrap();
    //Text::new(&buf, Point::new(0,14), style).draw(&mut display).unwrap();

    buf.clear();
    write!(&mut buf, "HW ver = {}", HW_VERSION.load(Ordering::Relaxed)).unwrap();
    Text::with_text_style(&buf, Point::new(0,14), header_style, text_style)
        .draw(&mut display).unwrap();

    loop {
        match DISPLAY_CHANNEL.receive().await {
            DisplayCmd::WriteLine(row, text) => {
                Text::with_text_style(&text, Point::new(0, row.into()), body_style,
                                      text_style).draw(&mut display).unwrap();
            }
            DisplayCmd::Clear => {
                    display.clear(Rgb565::BLACK).unwrap();
            }
        };
    }
}

#[cortex_m_rt::exception]
unsafe fn HardFault(ef: &cortex_m_rt::ExceptionFrame) -> ! {
    defmt::error!("HardFault! PC={:08x}", ef.pc());
    loop {}
}

#[embassy_executor::task]
async fn msc_task(msc: &'static mut msc::Msc<'static, HsDriver>) {
    info!("MSC task started");
    msc.run().await;
}
