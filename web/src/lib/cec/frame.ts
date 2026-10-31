import { FRAME_MAX, PHYS_ADDR_INVALID } from '$lib/usb/proto';
import { LOGICAL_ADDRESS_NAMES, LogicalAddress } from './constants';
import { OPCODE_NAMES, Opcode, OperandKind, type OperandSpec } from './opcodes';

const NIBBLE_BITS = 4;
const NIBBLE_MASK = 0xf;
const PHYS_ADDR_NIBBLES = 4;
const BYTE_MASK = 0xff;
const VENDOR_ID_BYTES = 3;
const HEX_RADIX = 16;
const PRINTABLE_ASCII = /^[\x20-\x7e]*$/;

export class FrameError extends Error {}

export function formatPhysAddr(pa: number): string {
  if (pa === PHYS_ADDR_INVALID) {
    return 'f.f.f.f';
  }
  return Array.from({ length: PHYS_ADDR_NIBBLES }, (_, i) =>
    ((pa >> ((PHYS_ADDR_NIBBLES - 1 - i) * NIBBLE_BITS)) & NIBBLE_MASK).toString(HEX_RADIX),
  ).join('.');
}

export function parsePhysAddr(text: string): number {
  const parts = text.trim().split('.');
  if (parts.length !== PHYS_ADDR_NIBBLES || parts.some((p) => !/^[0-9a-f]$/i.test(p))) {
    throw new FrameError(`"${text}" is not a physical address like 1.0.0.0`);
  }
  return parts.reduce((pa, p) => (pa << NIBBLE_BITS) | parseInt(p, HEX_RADIX), 0);
}

export function physAddrBytes(pa: number): number[] {
  return [(pa >> 8) & BYTE_MASK, pa & BYTE_MASK];
}

export function readPhysAddr(bytes: Uint8Array, offset: number): number {
  return (bytes[offset] << 8) | bytes[offset + 1];
}

export function parseHex(text: string): number[] {
  const clean = text.replace(/0x/gi, '').replace(/[\s:,]/g, '');
  if (clean.length % 2 !== 0 || !/^[0-9a-f]*$/i.test(clean)) {
    throw new FrameError(`"${text}" is not hex bytes`);
  }
  return Array.from({ length: clean.length / 2 }, (_, i) => parseInt(clean.slice(i * 2, i * 2 + 2), HEX_RADIX));
}

export function toHex(bytes: ArrayLike<number>, sep = ':'): string {
  return Array.from(bytes, (b) => b.toString(HEX_RADIX).padStart(2, '0')).join(sep);
}

export function encodeOperand(spec: OperandSpec, value: string): number[] {
  switch (spec.kind) {
    case OperandKind.ENUM:
      return [Number(value) & BYTE_MASK];
    case OperandKind.BYTE: {
      const bytes = parseHex(value);
      if (bytes.length !== 1) {
        throw new FrameError(`${spec.name}: one byte of hex`);
      }
      return bytes;
    }
    case OperandKind.PHYS_ADDR:
      return physAddrBytes(parsePhysAddr(value));
    case OperandKind.ASCII:
      if (value.length > spec.maxLength || !PRINTABLE_ASCII.test(value)) {
        throw new FrameError(`${spec.name}: up to ${spec.maxLength} printable ASCII characters`);
      }
      return Array.from(value, (c) => c.charCodeAt(0));
    case OperandKind.VENDOR_ID: {
      const bytes = parseHex(value.padStart(VENDOR_ID_BYTES * 2, '0'));
      if (bytes.length !== VENDOR_ID_BYTES) {
        throw new FrameError(`${spec.name}: three bytes of hex`);
      }
      return bytes;
    }
    case OperandKind.HEX:
      return parseHex(value);
  }
}

export function header(initiator: LogicalAddress, dest: LogicalAddress): number {
  return ((initiator & NIBBLE_MASK) << NIBBLE_BITS) | (dest & NIBBLE_MASK);
}

export function buildFrame(
  initiator: LogicalAddress,
  dest: LogicalAddress,
  opcode?: Opcode,
  operands: number[] = [],
): Uint8Array {
  const bytes = opcode === undefined ? [header(initiator, dest)] : [header(initiator, dest), opcode, ...operands];
  if (bytes.length > FRAME_MAX) {
    throw new FrameError(`frame is ${bytes.length} bytes, max ${FRAME_MAX}`);
  }
  return Uint8Array.from(bytes);
}

export interface DecodedFrame {
  initiator: LogicalAddress;
  dest: LogicalAddress;
  opcode?: Opcode;
  operands: Uint8Array;
}

export function decodeFrame(frame: Uint8Array): DecodedFrame {
  return {
    initiator: (frame[0] >> NIBBLE_BITS) as LogicalAddress,
    dest: (frame[0] & NIBBLE_MASK) as LogicalAddress,
    opcode: frame.length > 1 ? (frame[1] as Opcode) : undefined,
    operands: frame.subarray(2),
  };
}

export function describeFrame(frame: Uint8Array): string {
  const f = decodeFrame(frame);
  const route = `${LOGICAL_ADDRESS_NAMES[f.initiator]} → ${LOGICAL_ADDRESS_NAMES[f.dest]}`;
  if (f.opcode === undefined) {
    return `${route}: poll`;
  }
  const name = OPCODE_NAMES.get(f.opcode) ?? `opcode 0x${f.opcode.toString(HEX_RADIX).padStart(2, '0')}`;
  const detail = describeOperands(f);
  return detail ? `${route}: ${name} (${detail})` : `${route}: ${name}`;
}

function describeOperands(f: DecodedFrame): string {
  const ops = f.operands;
  switch (f.opcode) {
    case Opcode.ACTIVE_SOURCE:
    case Opcode.INACTIVE_SOURCE:
    case Opcode.SET_STREAM_PATH:
    case Opcode.ROUTING_INFORMATION:
    case Opcode.REPORT_PHYSICAL_ADDRESS:
    case Opcode.SYSTEM_AUDIO_MODE_REQUEST:
      return ops.length >= 2 ? formatPhysAddr(readPhysAddr(ops, 0)) : '';
    case Opcode.ROUTING_CHANGE:
      return ops.length >= 4
        ? `${formatPhysAddr(readPhysAddr(ops, 0))} → ${formatPhysAddr(readPhysAddr(ops, 2))}`
        : '';
    case Opcode.SET_OSD_NAME:
    case Opcode.SET_MENU_LANGUAGE:
      return `"${String.fromCharCode(...ops)}"`;
    case Opcode.FEATURE_ABORT:
      return ops.length >= 2 ? `${OPCODE_NAMES.get(ops[0]) ?? toHex([ops[0]])}, reason ${ops[1]}` : '';
    default:
      return ops.length ? toHex(ops) : '';
  }
}
