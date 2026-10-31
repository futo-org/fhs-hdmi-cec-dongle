import { describe, expect, it } from 'vitest';
import { LogicalAddress } from './constants';
import { buildFrame, describeFrame, encodeOperand, formatPhysAddr, parseHex, parsePhysAddr } from './frame';
import { Opcode, OperandKind, OPCODES } from './opcodes';

describe('physical address', () => {
  it('round-trips', () => {
    expect(parsePhysAddr('1.2.0.f')).toBe(0x120f);
    expect(formatPhysAddr(0x120f)).toBe('1.2.0.f');
  });

  it('rejects malformed text', () => {
    expect(() => parsePhysAddr('1.0.0')).toThrow();
    expect(() => parsePhysAddr('1.0.0.g')).toThrow();
  });
});

describe('frames', () => {
  it('builds One Touch Play from playback 1', () => {
    expect(Array.from(buildFrame(LogicalAddress.PLAYBACK_1, LogicalAddress.TV, Opcode.IMAGE_VIEW_ON))).toEqual([
      0x40, 0x04,
    ]);
    expect(
      Array.from(buildFrame(LogicalAddress.PLAYBACK_1, LogicalAddress.BROADCAST, Opcode.ACTIVE_SOURCE, [0x10, 0x00])),
    ).toEqual([0x4f, 0x82, 0x10, 0x00]);
  });

  it('builds a poll as a header only', () => {
    expect(Array.from(buildFrame(LogicalAddress.PLAYBACK_1, LogicalAddress.PLAYBACK_1))).toEqual([0x44]);
  });

  it('rejects oversized frames', () => {
    expect(() => buildFrame(LogicalAddress.TV, LogicalAddress.TV, Opcode.VENDOR_COMMAND, new Array(15).fill(0))).toThrow();
  });

  it('describes a received frame', () => {
    expect(describeFrame(Uint8Array.from([0x0f, 0x84, 0x00, 0x00, 0x00]))).toBe(
      'TV → Broadcast: Report Physical Address (0.0.0.0)',
    );
  });
});

describe('operands', () => {
  it('encodes each kind', () => {
    expect(encodeOperand({ kind: OperandKind.PHYS_ADDR, name: '' }, '2.1.0.0')).toEqual([0x21, 0x00]);
    expect(encodeOperand({ kind: OperandKind.ASCII, name: '', maxLength: 3 }, 'eng')).toEqual([0x65, 0x6e, 0x67]);
    expect(encodeOperand({ kind: OperandKind.VENDOR_ID, name: '' }, '8045')).toEqual([0x00, 0x80, 0x45]);
    expect(parseHex('0x01 02:ff')).toEqual([0x01, 0x02, 0xff]);
  });

  it('rejects text that is too long', () => {
    expect(() => encodeOperand({ kind: OperandKind.ASCII, name: '', maxLength: 3 }, 'english')).toThrow();
  });
});

describe('opcode table', () => {
  it('lists each opcode once', () => {
    const opcodes = OPCODES.map((s) => s.opcode);
    expect(new Set(opcodes).size).toBe(opcodes.length);
  });

  it('covers every opcode in the enum', () => {
    const listed = new Set(OPCODES.map((s) => s.opcode));
    const all = Object.values(Opcode).filter((v): v is Opcode => typeof v === 'number');
    expect(all.filter((op) => !listed.has(op))).toEqual([]);
  });
});
