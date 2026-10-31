import { EDID_BLOCK_SIZE, PHYS_ADDR_INVALID } from '$lib/usb/proto';
import { readPhysAddr } from './frame';

const EXT_COUNT_OFFSET = 126;
const CTA_EXT_TAG = 0x02;
const CTA_DTD_OFFSET = 2;
const CTA_DATA_BLOCKS_OFFSET = 4;
const DATA_BLOCK_TAG_SHIFT = 5;
const DATA_BLOCK_LEN_MASK = 0x1f;
const VENDOR_SPECIFIC_TAG = 3;
const HDMI_OUI = [0x03, 0x0c, 0x00];
const VSDB_PHYS_ADDR_OFFSET = 4;
const BYTE_MASK = 0xff;

export function checksumOk(block: Uint8Array): boolean {
  return (block.reduce((sum, b) => sum + b, 0) & BYTE_MASK) === 0;
}

export function extensionCount(base: Uint8Array): number {
  return base[EXT_COUNT_OFFSET];
}

// From the HDMI vendor-specific data block of a CTA-861 extension.
export function edidPhysAddr(blocks: Uint8Array[]): number {
  for (const ext of blocks.slice(1)) {
    if (ext.length < EDID_BLOCK_SIZE || ext[0] !== CTA_EXT_TAG) {
      continue;
    }
    const end = ext[CTA_DTD_OFFSET];
    let i = CTA_DATA_BLOCKS_OFFSET;
    while (i < end) {
      const tag = ext[i] >> DATA_BLOCK_TAG_SHIFT;
      const len = ext[i] & DATA_BLOCK_LEN_MASK;
      const isHdmiVsdb =
        tag === VENDOR_SPECIFIC_TAG && len >= VSDB_PHYS_ADDR_OFFSET + 1 && HDMI_OUI.every((b, j) => ext[i + 1 + j] === b);
      if (isHdmiVsdb) {
        return readPhysAddr(ext, i + VSDB_PHYS_ADDR_OFFSET);
      }
      i += len + 1;
    }
  }
  return PHYS_ADDR_INVALID;
}
