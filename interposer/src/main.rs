#![no_std]
#![no_main]

use defmt::info;
use defmt_rtt as _;
use embassy_executor::Spawner;
use embassy_stm32::gpio::{Level, Output, Speed};
use embassy_stm32::rcc::{
    AHBPrescaler, APBPrescaler, Hse, HseMode, Pll, PllMul, PllPDiv, PllPreDiv, PllQDiv,
    PllSource, Sysclk,
};
use embassy_stm32::time::Hertz;
use embassy_stm32::Config;
use embassy_time::Timer;
use panic_probe as _;

defmt::timestamp!("{=u64:us}", embassy_time::Instant::now().as_micros());

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
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
    let mut led = Output::new(p.PE9,  Level::Low, Speed::Low); // BLUE
    let _led_g  = Output::new(p.PE11, Level::Low, Speed::Low); // GREEN
    let _led_y  = Output::new(p.PE13, Level::Low, Speed::Low); // YELLOW
    let _rgb_r  = Output::new(p.PB4,  Level::Low, Speed::Low); // RGB RED
    let _rgb_g  = Output::new(p.PB5,  Level::Low, Speed::Low); // RGB GREEN
    let _rgb_b  = Output::new(p.PB6,  Level::Low, Speed::Low); // RGB BLUE

    info!("STM32F723 Embassy bringup — SYSCLK=216MHz");

    loop {
        led.set_high();
        Timer::after_millis(500).await;
        led.set_low();
        Timer::after_millis(500).await;
        info!("blink");
    }
}
