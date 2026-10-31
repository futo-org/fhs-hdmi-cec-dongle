import type { TxStatus } from '$lib/usb/proto';
import type { DeviceType, LogicalAddress } from './constants';
import type { DecodedFrame } from './frame';
import type { Opcode } from './opcodes';

export type FramePredicate = (frame: DecodedFrame) => boolean;

// What multi-step procedures need from a connected session.
export interface CecBus {
  readonly logicalAddress: LogicalAddress;
  readonly physAddr: number;
  readonly deviceType: DeviceType;
  send(dest: LogicalAddress, opcode: Opcode, operands?: number[]): Promise<TxStatus>;
  poll(dest: LogicalAddress, initiator?: LogicalAddress): Promise<TxStatus>;
  waitFor(predicate: FramePredicate, timeoutMs: number): Promise<DecodedFrame | null>;
  claimLogicalAddress(la: LogicalAddress): Promise<void>;
  setActiveSource(active: boolean): void;
  note(text: string): void;
}
