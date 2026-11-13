// Mirrors driver/fhs-cec-proto.h. Multi-byte fields are little-endian.

export const USB_VID = 0x3a4c;
export const USB_PID = 0x4663;

export const USB_CLASS = 0xff;
export const USB_SUBCLASS = 0x01;
export const USB_PROTOCOL = 0x01;

export const PROTO_MAJOR = 1;

export const FRAME_MAX = 16;
export const EVENT_PACKET_SIZE = 32;
export const TX_BUDGET_MS = 1000;
export const PHYS_ADDR_INVALID = 0xffff;

export const EDID_BLOCK_SIZE = 128;
export const EDID_MAX_BLOCKS = 2;

export enum Request {
  GET_INFO = 0x01,
  SET_ENABLE = 0x02,
  SET_LOG_ADDRS = 0x03,
  SET_MONITOR_ALL = 0x04,
  TRANSMIT = 0x05,
  SET_PHYS_ADDR = 0x06,
  GET_STATUS = 0x07,
  GET_EDID = 0x08,
}

export enum Cap {
  MONITOR_ALL = 1 << 0,
  EDID = 1 << 1,
}

export enum EventType {
  TX_DONE = 0x01,
  RX = 0x02,
}

export enum TxStatus {
  OK = 0x00,
  NACK = 0x01,
  ARB_LOST = 0x02,
  LOW_DRIVE = 0x03,
  ERROR = 0x04,
}

export const TX_STATUS_NAMES: Record<TxStatus, string> = {
  [TxStatus.OK]: 'OK',
  [TxStatus.NACK]: 'NACK',
  [TxStatus.ARB_LOST]: 'ARB LOST',
  [TxStatus.LOW_DRIVE]: 'LOW DRIVE',
  [TxStatus.ERROR]: 'ERROR',
};

export interface Info {
  protoMajor: number;
  protoMinor: number;
  fwVersion: string;
  physAddr: number;
  caps: number;
}

export const INFO_SIZE = 12;

export function parseInfo(view: DataView): Info {
  return {
    protoMajor: view.getUint8(0),
    protoMinor: view.getUint8(1),
    fwVersion: `${view.getUint8(2)}.${view.getUint8(3)}.${view.getUint8(4)}`,
    physAddr: view.getUint16(6, true),
    caps: view.getUint32(8, true),
  };
}

export const STATUS_FIELDS = [
  'rxFrames',
  'rxErrors',
  'rxDropped',
  'txFrames',
  'txNack',
  'txArbLost',
  'txLowDrive',
  'txError',
  'eventsDropped',
  'recoveries',
] as const;

export type Status = Record<(typeof STATUS_FIELDS)[number], number>;

export const STATUS_SIZE = STATUS_FIELDS.length * Uint32Array.BYTES_PER_ELEMENT;

export function parseStatus(view: DataView): Status {
  return Object.fromEntries(
    STATUS_FIELDS.map((name, i) => [name, view.getUint32(i * Uint32Array.BYTES_PER_ELEMENT, true)]),
  ) as Status;
}

export type DongleEvent =
  | { type: EventType.TX_DONE; seq: number; status: TxStatus }
  | { type: EventType.RX; frame: Uint8Array };

export function parseEvent(view: DataView): DongleEvent | null {
  if (view.byteLength < 1) {
    return null;
  }
  switch (view.getUint8(0)) {
    case EventType.TX_DONE:
      return { type: EventType.TX_DONE, seq: view.getUint8(1), status: view.getUint8(2) };
    case EventType.RX: {
      const len = view.getUint8(1);
      return { type: EventType.RX, frame: new Uint8Array(view.buffer, view.byteOffset + 2, len).slice() };
    }
    default:
      return null;
  }
}
