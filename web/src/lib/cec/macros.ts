import { TxStatus, TX_STATUS_NAMES } from '$lib/usb/proto';
import type { CecBus } from './bus';
import {
  ALL_LOGICAL_ADDRESSES,
  CecVersion,
  LOGICAL_ADDRESS_CANDIDATES,
  LOGICAL_ADDRESS_NAMES,
  LogicalAddress,
  PowerStatus,
  UNREGISTERED,
  UiCommand,
} from './constants';
import { formatPhysAddr, physAddrBytes, readPhysAddr, toHex, type DecodedFrame } from './frame';
import { Opcode } from './opcodes';

export const REPLY_TIMEOUT_MS = 1000;
const POWER_STATUS_LABELS: Record<PowerStatus, string> = {
  [PowerStatus.ON]: 'on',
  [PowerStatus.STANDBY]: 'standby',
  [PowerStatus.TO_ON]: 'turning on',
  [PowerStatus.TO_STANDBY]: 'going to standby',
};
const CEC_VERSION_LABELS: Record<number, string> = {
  [CecVersion.V1_3A]: '1.3a',
  [CecVersion.V1_4]: '1.4',
  [CecVersion.V2_0]: '2.0',
};

export class MacroError extends Error {}

function expectOk(status: TxStatus, what: string): void {
  if (status !== TxStatus.OK) {
    throw new MacroError(`${what}: ${TX_STATUS_NAMES[status]}`);
  }
}

// Sends a request and waits for its reply or a Feature Abort.
export async function query(
  bus: CecBus,
  dest: LogicalAddress,
  request: Opcode,
  reply: Opcode,
  operands: number[] = [],
): Promise<DecodedFrame | null> {
  const answer = bus.waitFor(
    (f) =>
      f.initiator === dest &&
      (f.opcode === reply || (f.opcode === Opcode.FEATURE_ABORT && f.operands[0] === request)),
    REPLY_TIMEOUT_MS,
  );
  const status = await bus.send(dest, request, operands);
  if (status !== TxStatus.OK) {
    return null;
  }
  const frame = await answer;
  return frame?.opcode === reply ? frame : null;
}

export async function allocateLogicalAddress(bus: CecBus): Promise<LogicalAddress> {
  const candidates = LOGICAL_ADDRESS_CANDIDATES[bus.deviceType] ?? [];
  let claimed: LogicalAddress = UNREGISTERED;
  for (const la of candidates) {
    if ((await bus.poll(la, la)) === TxStatus.NACK) {
      claimed = la;
      break;
    }
  }
  await bus.claimLogicalAddress(claimed);
  if (claimed !== UNREGISTERED) {
    await bus.send(LogicalAddress.BROADCAST, Opcode.REPORT_PHYSICAL_ADDRESS, [
      ...physAddrBytes(bus.physAddr),
      bus.deviceType,
    ]);
  }
  bus.note(`claimed ${LOGICAL_ADDRESS_NAMES[claimed]} (${claimed})`);
  return claimed;
}

export async function oneTouchPlay(bus: CecBus): Promise<void> {
  expectOk(await bus.send(LogicalAddress.TV, Opcode.IMAGE_VIEW_ON), 'Image View On');
  expectOk(
    await bus.send(LogicalAddress.BROADCAST, Opcode.ACTIVE_SOURCE, physAddrBytes(bus.physAddr)),
    'Active Source',
  );
  bus.setActiveSource(true);
}

export async function inactiveSource(bus: CecBus): Promise<void> {
  expectOk(await bus.send(LogicalAddress.TV, Opcode.INACTIVE_SOURCE, physAddrBytes(bus.physAddr)), 'Inactive Source');
  bus.setActiveSource(false);
}

export async function standby(bus: CecBus, dest: LogicalAddress): Promise<void> {
  expectOk(await bus.send(dest, Opcode.STANDBY), 'Standby');
  bus.setActiveSource(false);
}

export async function pressKey(bus: CecBus, dest: LogicalAddress, key: UiCommand): Promise<void> {
  expectOk(await bus.send(dest, Opcode.USER_CONTROL_PRESSED, [key]), 'User Control Pressed');
  expectOk(await bus.send(dest, Opcode.USER_CONTROL_RELEASED), 'User Control Released');
}

export async function switchInput(bus: CecBus, pa: number): Promise<void> {
  expectOk(await bus.send(LogicalAddress.BROADCAST, Opcode.SET_STREAM_PATH, physAddrBytes(pa)), 'Set Stream Path');
}

export async function powerStatus(bus: CecBus, dest: LogicalAddress): Promise<PowerStatus | null> {
  const reply = await query(bus, dest, Opcode.GIVE_DEVICE_POWER_STATUS, Opcode.REPORT_POWER_STATUS);
  const status = reply ? (reply.operands[0] as PowerStatus) : null;
  bus.note(`${LOGICAL_ADDRESS_NAMES[dest]} power: ${status === null ? 'no reply' : POWER_STATUS_LABELS[status]}`);
  return status;
}

export async function wakeAndWait(bus: CecBus, timeoutMs: number, intervalMs: number): Promise<boolean> {
  await oneTouchPlay(bus);
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if ((await powerStatus(bus, LogicalAddress.TV)) === PowerStatus.ON) {
      return true;
    }
    await new Promise((r) => setTimeout(r, intervalMs));
  }
  return false;
}

export async function systemAudioOn(bus: CecBus): Promise<boolean> {
  const answer = bus.waitFor(
    (f) => f.initiator === LogicalAddress.AUDIO_SYSTEM && f.opcode === Opcode.SET_SYSTEM_AUDIO_MODE,
    REPLY_TIMEOUT_MS,
  );
  expectOk(
    await bus.send(LogicalAddress.AUDIO_SYSTEM, Opcode.SYSTEM_AUDIO_MODE_REQUEST, physAddrBytes(bus.physAddr)),
    'System Audio Mode Request',
  );
  const reply = await answer;
  const on = reply?.operands[0] === 1;
  bus.note(`system audio mode: ${reply ? (on ? 'on' : 'off') : 'no reply'}`);
  return on;
}

export interface DeviceReport {
  la: LogicalAddress;
  physAddr?: string;
  osdName?: string;
  vendorId?: string;
  cecVersion?: string;
  power?: string;
}

export async function describeDevice(bus: CecBus, la: LogicalAddress): Promise<DeviceReport> {
  const report: DeviceReport = { la };
  const pa = await query(bus, la, Opcode.GIVE_PHYSICAL_ADDRESS, Opcode.REPORT_PHYSICAL_ADDRESS);
  if (pa && pa.operands.length >= 2) {
    report.physAddr = formatPhysAddr(readPhysAddr(pa.operands, 0));
  }
  const name = await query(bus, la, Opcode.GIVE_OSD_NAME, Opcode.SET_OSD_NAME);
  if (name) {
    report.osdName = String.fromCharCode(...name.operands);
  }
  const vendor = await query(bus, la, Opcode.GIVE_DEVICE_VENDOR_ID, Opcode.DEVICE_VENDOR_ID);
  if (vendor) {
    report.vendorId = toHex(vendor.operands, '');
  }
  const version = await query(bus, la, Opcode.GET_CEC_VERSION, Opcode.CEC_VERSION);
  if (version) {
    report.cecVersion = CEC_VERSION_LABELS[version.operands[0]] ?? String(version.operands[0]);
  }
  const power = await query(bus, la, Opcode.GIVE_DEVICE_POWER_STATUS, Opcode.REPORT_POWER_STATUS);
  if (power) {
    report.power = POWER_STATUS_LABELS[power.operands[0] as PowerStatus];
  }
  return report;
}

export async function scanBus(bus: CecBus, onDevice: (report: DeviceReport) => void): Promise<void> {
  const others = ALL_LOGICAL_ADDRESSES.filter((la) => la !== LogicalAddress.BROADCAST && la !== bus.logicalAddress);
  for (const la of others) {
    if ((await bus.poll(la)) === TxStatus.OK) {
      onDevice(await describeDevice(bus, la));
    }
  }
  bus.note('scan done');
}
