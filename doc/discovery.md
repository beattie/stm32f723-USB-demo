# Use 32F723EDISCOVERY board to develop Rust device

https://www.st.com/en/evaluation-tools/32f723ediscovery.html

Start over with board design using 32F723EDISCOVERY as a guide for USB.

For the software purchase a 32F723EDISCOVERY and develop USB drivers.

## Reference: Synopsys DWC_OTG IP Programming Guide

The STM32F723 OTG_FS and OTG_HS peripherals are both Synopsys DesignWare
USB 2.0 Hi-Speed OTG (DWC_OTG) cores.  The authoritative initialization
sequence comes from:

  **"DesignWare Cores USB 2.0 Hi-Speed On-The-Go (OTG) Databook"**
  Synopsys, Inc. (requires Synopsys SolvNet partner login to download)

The same initialization sequence is summarized in:

  **RM0431 §34.17** (OTG_HS) and **§33.17** (OTG_FS)
  https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf

Key sections:
  - §34.17.1  Device initialization sequence (CSRST required)
  - §34.17.2  Host initialization sequence
  - §34.13.2  GUSBCFG — TRDT, PHYSEL, FDMOD/FHMOD
  - §34.13.6  GCCFG — PWRDWN, VBDEN

