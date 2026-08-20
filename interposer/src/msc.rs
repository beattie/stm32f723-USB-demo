#![allow(dead_code)]

use defmt::info;
use embassy_usb::driver::{Driver, EndpointIn, EndpointOut, Endpoint};
use embassy_usb::Builder;

const CBW_SIGNATURE: u32 = 0x4342_5355; // "USBC" little-endian
const CSW_SIGNATURE: u32 = 0x5342_5355; // "USBS" little-endian

const CSW_OK:   u8 = 0;
const CSW_FAIL: u8 = 1;

const SCSI_TEST_UNIT_READY:     u8 = 0x00;
const SCSI_REQUEST_SENSE:       u8 = 0x03;
const SCSI_INQUIRY:             u8 = 0x12;
const SCSI_MODE_SENSE_6:        u8 = 0x1A;
const SCSI_START_STOP_UNIT:     u8 = 0x1B;
const SCSI_PREVENT_ALLOW:       u8 = 0x1E;
const SCSI_READ_CAPACITY_10:    u8 = 0x25;
const SCSI_READ_10:             u8 = 0x28;
const SCSI_WRITE_10:            u8 = 0x2A;

pub struct Msc<'d, D: Driver<'d>> {
    ep_out: D::EndpointOut,
    ep_in:  D::EndpointIn,
}

pub trait SdAccess {
    async fn read_sector(&mut self, lba: u32, buf: &mut [u8; 512]) -> bool;
    async fn write_sector(&mut self, lba: u32, buf: &[u8; 512]) -> bool;
    fn capacity_sectors(&self) -> u32;
}

impl<'d, D: Driver<'d>> Msc<'d, D> {
    pub fn new(builder: &mut Builder<'d, D>) -> Self {
        let (ep_out, ep_in) = {
            let mut func = builder.function(0x08, 0x06, 0x50);
            let mut iface = func.interface();
            let mut alt = iface.alt_setting(0x08, 0x06, 0x50, None);
            let ep_out = alt.endpoint_bulk_out(None, 512);
            let ep_in  = alt.endpoint_bulk_in(None, 512);
            (ep_out, ep_in)
        };
        Self { ep_out, ep_in }
    }

    pub async fn run<S: SdAccess>(&mut self, sd: &mut S) {
        self.ep_out.wait_enabled().await;
        info!("MSC: endpoint enabled");
        loop {
            let mut cbw = [0u8; 512];
            let n = match self.ep_out.read(&mut cbw).await {
                Ok(n) => n,
                Err(_) => { self.ep_out.wait_enabled().await; continue; }
            };
            if n < 31 {
                info!("MSC: short CBW ({})", n);
                continue;
            }

            let sig = u32::from_le_bytes(cbw[0..4].try_into().unwrap());
            if sig != CBW_SIGNATURE {
                info!("MSC: bad CBW signature {:08x}", sig);
                continue;
            }

            let tag    = u32::from_le_bytes(cbw[4..8].try_into().unwrap());
            let length = u32::from_le_bytes(cbw[8..12].try_into().unwrap());
            let flags  = cbw[12];
            let cb_len = (cbw[14] & 0x1F) as usize;
            let cb     = &cbw[15..15 + cb_len.min(16)];

            info!("MSC: cmd={:02x} len={} flags={:02x}", cb[0], length, flags);

            let status = self.handle_scsi(cb, flags, length, sd).await;

            let mut csw = [0u8; 13];
            csw[0..4].copy_from_slice(&CSW_SIGNATURE.to_le_bytes());
            csw[4..8].copy_from_slice(&tag.to_le_bytes());
            csw[8..12].copy_from_slice(&0u32.to_le_bytes());
            csw[12] = status;
            let _ = self.write_all(&csw).await;
        }
    }

    async fn handle_scsi<S: SdAccess>
            (&mut self, cb: &[u8], flags: u8, length: u32, sd: &mut S) -> u8 {
        let dir_in = flags & 0x80 != 0;
        match cb[0] {
            SCSI_TEST_UNIT_READY => {
                info!("MSC: TEST UNIT READY");
                CSW_OK
            }
            SCSI_INQUIRY => {
                info!("MSC: INQUIRY");
                if dir_in {
                    let mut r = [0u8; 36];
                    r[0] = 0x00;    // direct access block device
                    r[1] = 0x80;    // removable
                    r[2] = 0x02;    // SCSI-2
                    r[3] = 0x02;    // response data format
                    r[4] = 31;      // additional length = 36 - 5
                    r[8..16].copy_from_slice(b"Interpos");
                    r[16..32].copy_from_slice(b"SD Card         ");
                    r[32..36].copy_from_slice(b"1.00");
                    let n = (length as usize).min(r.len());
                    let _ = self.write_all(&r[..n]).await;
                }
                CSW_OK
            }
            SCSI_REQUEST_SENSE => {
                info!("MSC: REQUEST SENSE");
                if dir_in {
                    let mut r = [0u8; 18];
                    r[0] = 0x70;    // current errors
                    r[2] = 0x00;    // no sense
                    r[7] = 10;
                    let n = (length as usize).min(r.len());
                    let _ = self.write_all(&r[..n]).await;
                }
                CSW_OK
            }
            SCSI_MODE_SENSE_6 => {
                info!("MSC: MODE SENSE(6)");
                if dir_in {
                    let r = [0x03u8, 0x00, 0x00, 0x00];
                    let n = (length as usize).min(r.len());
                    let _ = self.write_all(&r[..n]).await;
                }
                CSW_OK
            }
            SCSI_START_STOP_UNIT | SCSI_PREVENT_ALLOW => CSW_OK,
            SCSI_READ_CAPACITY_10 => {
                info!("MSC: READ CAPACITY(10)");
                let last_lba = sd.capacity_sectors() - 1;
                let mut r = [0u8; 8];
                r[0..4].copy_from_slice(&last_lba.to_be_bytes());   // last LBA
                r[4..8].copy_from_slice(&512u32.to_be_bytes());
                let _ = self.write_all(&r).await;
                CSW_OK
            }
            SCSI_READ_10 => {
                let lba     = u32::from_be_bytes(cb[2..6].try_into().unwrap());
                let sectors = u16::from_be_bytes(cb[7..9].try_into().unwrap());
                info!("MSC: READ(10) lba={} sectors={}", lba, sectors);
                let mut buf = [0u8; 512];
                for i in 0..sectors {
                    if !sd.read_sector(lba + i as u32, &mut buf).await {
                        return CSW_FAIL;
                    }
                    if self.write_all(&buf).await.is_err() {
                        return CSW_FAIL;
                    }
                }
                CSW_OK
            }
            SCSI_WRITE_10 => {
                let lba     = u32::from_be_bytes(cb[2..6].try_into().unwrap());
                let sectors = u16::from_be_bytes(cb[7..9].try_into().unwrap());
                info!("MSC: WRITE(10) lba={} sectors={}", lba, sectors);
                let mut buf = [0u8; 512];
                for i in 0..sectors {
                    if self.read_sector_buf(&mut buf).await.is_err() {
                        return CSW_FAIL;
                    }
                    if !sd.write_sector(lba + i as u32, &buf).await {
                        return CSW_FAIL;
                    }
                }
                CSW_OK
            }
            op => {
                info!("MSC: unknown opcode {:02x}", op);
                CSW_FAIL
            }
        }
    }

    async fn read_exact(&mut self, buf: &mut [u8]) -> Result<(), ()> {
        let mut pos = 0;
        while pos < buf.len() {
            let n = self.ep_out.read(&mut buf[pos..]).await.map_err(|_| ())?;
            pos += n;
        }
        Ok(())
    }

    async fn write_all(&mut self, buf: &[u8]) -> Result<(), ()> {
        let mut pos = 0;
        while pos < buf.len() {
            let end = (pos + 512).min(buf.len());
            self.ep_in.write(&buf[pos..end]).await.map_err(|_| ())?;
            pos = end;
        }
        Ok(())
    }

    async fn read_sector_buf(&mut self, buf: &mut [u8; 512]) ->
                Result<(), ()> {
        let mut pos = 0;
        while pos < 512 {
            let n = self.ep_out.read(&mut buf[pos..]).await.map_err(|_| ())?;
            pos += n;
        }
        Ok(())
    }

}
