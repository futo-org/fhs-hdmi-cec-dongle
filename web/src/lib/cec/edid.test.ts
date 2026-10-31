import { describe, expect, it } from 'vitest';
import { PHYS_ADDR_INVALID } from '$lib/usb/proto';
import { checksumOk, edidPhysAddr } from './edid';

const BLOCK = 128;

function withChecksum(block: Uint8Array): Uint8Array {
  const sum = block.subarray(0, BLOCK - 1).reduce((s, b) => s + b, 0);
  block[BLOCK - 1] = (0x100 - (sum & 0xff)) & 0xff;
  return block;
}

function ctaExtension(dataBlocks: number[]): Uint8Array {
  const ext = new Uint8Array(BLOCK);
  ext.set([0x02, 0x03, 4 + dataBlocks.length, 0x00]);
  ext.set(dataBlocks, 4);
  return withChecksum(ext);
}

describe('edidPhysAddr', () => {
  it('reads the HDMI vendor block after another data block', () => {
    const audio = [0x23, 0x09, 0x07, 0x07];
    const hdmiVsdb = [0x65, 0x03, 0x0c, 0x00, 0x21, 0x00];
    const ext = ctaExtension([...audio, ...hdmiVsdb]);
    expect(checksumOk(ext)).toBe(true);
    expect(edidPhysAddr([new Uint8Array(BLOCK), ext])).toBe(0x2100);
  });

  it('is invalid without an HDMI vendor block', () => {
    expect(edidPhysAddr([new Uint8Array(BLOCK), ctaExtension([0x23, 0x09, 0x07, 0x07])])).toBe(PHYS_ADDR_INVALID);
    expect(edidPhysAddr([new Uint8Array(BLOCK)])).toBe(PHYS_ADDR_INVALID);
  });
});
