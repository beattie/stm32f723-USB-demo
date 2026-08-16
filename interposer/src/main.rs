#![no_std]
#![no_main]

use defmt::info;
use defmt_rtt as _;
use embassy_executor::Spawner;

use embassy_futures::select::{select, Either};

use embassy_stm32::Peri;
use embassy_stm32::gpio::{AfType, Flex, Level, Output, OutputType};
// rename Speed to avoid clash with USB Speed:
use embassy_stm32::gpio::Speed as GpioSpeed;
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

use embassy_usb_synopsys_otg::{PhyType, otg_v1::Otg};
use embassy_usb_synopsys_otg::host::{
    HostState, OtgHost, OtgHostInstance, on_host_interrupt
};
use embassy_usb_synopsys_otg::host::OtgHostAllocator;

use embassy_usb_host::{BusState, BusRoute, bus};
use embassy_usb_host::class::kbd::{KbdEvent, KbdHandler};
use embassy_usb_host::handler::HandlerEvent;
use embassy_usb_host::{BusController, BusHandle};

use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

use embassy_time::Timer;
use panic_probe as _;

defmt::timestamp!("{=u64:us}", embassy_time::Instant::now().as_micros());

#[derive(Clone, Copy)]
enum LedCmd {
    KeyboardConnected,
    KeyboardDisconnected,
    NotAKeyboard,
}

static LED_CMD: Signal<CriticalSectionRawMutex, LedCmd> = Signal::new();

static HOST_STATE: HostState<8> = HostState::new();
static BUS_STATE: BusState = BusState::new();

struct OtgFsHostIrq;

impl Handler<interrupt::typelevel::OTG_FS> for OtgFsHostIrq {
    unsafe fn on_interrupt() {
        let r = unsafe { Otg::from_ptr(pac::USB_OTG_FS.as_ptr() as *mut ()) };
        on_host_interrupt(r, &HOST_STATE, 8);
    }
}

bind_interrupts!(struct Irqs {
    OTG_FS => OtgFsHostIrq;
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

    spawner.spawn(led_task(p.PE9, p.PE11, p.PE13).unwrap());
    spawner.spawn(kbd_task(controller, handle, p.PB0).unwrap());

    // All LEDs off at init (active high; RGB red has enough leakage to glow if floating)
    let _rgb_r = Output::new(p.PB4,  Level::Low , GpioSpeed::Low); // RGB RED
    let _rgb_g = Output::new(p.PB5,  Level::Low , GpioSpeed::Low); // RGB GREEN
    let _rgb_b = Output::new(p.PB6,  Level::Low , GpioSpeed::Low); // RGB BLUE

    info!("STM32F723 Embassy bringup — SYSCLK=216MHz");

    loop {
        Timer::after_millis(1000).await;
    }
}

#[embassy_executor::task]
async fn kbd_task(
    mut controller: BusController<'static, OtgHost<'static, 8>>,
    handle: BusHandle<'static, OtgHostAllocator<'static, 8>>,
    vbus_ena: Peri<'static, embassy_stm32::peripherals::PB0>,
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
/*
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

---------------------
            match kbd.wait_for_event().await {
                Ok(HandlerEvent::HandlerEvent(KbdEvent::
                                              KeyStatusUpdate(update))) => {
                    info!("mod={:08b} keys={:?}",
                          update.modifiers, update.keypress);
                }
                Ok(_) => {}
                Err(e) => {
                    LED_CMD.signal(LedCmd::KeyboardDisconnected);
                    info!("Keyboard disconnected: {:?}", e);
                    handle.free_address(enum_info.device_address);
                    break;
                }
            }
        }
    }
}
*/

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
