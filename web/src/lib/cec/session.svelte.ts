import { Dongle } from '$lib/usb/dongle';
import {
  Cap,
  EDID_MAX_BLOCKS,
  PHYS_ADDR_INVALID,
  TX_STATUS_NAMES,
  TxStatus,
  type Info,
  type Status,
} from '$lib/usb/proto';
import type { CecBus, FramePredicate } from './bus';
import { CecVersion, DEFAULT_OSD_NAME, DeviceType, LogicalAddress, PowerStatus, UNREGISTERED } from './constants';
import { checksumOk, edidPhysAddr, extensionCount } from './edid';
import { buildFrame, decodeFrame, describeFrame, formatPhysAddr, physAddrBytes, toHex, type DecodedFrame } from './frame';
import { allocateLogicalAddress, type DeviceReport } from './macros';
import { Opcode } from './opcodes';

const LOG_LIMIT = 500;

export enum LogKind {
  TX = 'tx',
  RX = 'rx',
  NOTE = 'note',
  ERROR = 'error',
}

export interface LogEntry {
  id: string;
  time: Date;
  kind: LogKind;
  text: string;
  bytes?: string;
  status?: string;
}

interface Waiter {
  predicate: FramePredicate;
  resolve: (frame: DecodedFrame | null) => void;
}

export class CecSession implements CecBus {
  dongle = $state<Dongle | null>(null);
  info = $state<Info | null>(null);
  status = $state<Status | null>(null);
  physAddr = $state(PHYS_ADDR_INVALID);
  edidPhysAddr = $state<number | null>(null);
  logicalAddress = $state<LogicalAddress>(UNREGISTERED);
  deviceType = $state(DeviceType.PLAYBACK);
  osdName = $state(DEFAULT_OSD_NAME);
  monitorAll = $state(false);
  autoReply = $state(true);
  activeSource = $state(false);
  busy = $state<string | null>(null);
  log = $state<LogEntry[]>([]);
  devices = $state<DeviceReport[]>([]);

  private waiters = new Set<Waiter>();

  readonly supported = Dongle.isSupported();

  get connected(): boolean {
    return this.dongle !== null;
  }

  get hasEdid(): boolean {
    return ((this.info?.caps ?? 0) & Cap.EDID) !== 0;
  }

  constructor() {
    if (this.supported) {
      navigator.usb.addEventListener('disconnect', (e) => {
        if (this.dongle?.owns(e.device)) {
          this.dropConnection('unplugged');
        }
      });
    }
  }

  async connect(): Promise<void> {
    await this.run('connect', async () => {
      const dongle = await Dongle.request({
        onFrame: (frame) => this.onFrame(frame),
        onDisconnect: (reason) => this.dropConnection(reason),
      });
      this.dongle = dongle;
      this.info = await dongle.getInfo();
      this.physAddr = this.info.physAddr;
      this.note(`connected to ${dongle.name}, firmware ${this.info.fwVersion}`);
      if (this.hasEdid) {
        await this.readEdid();
      }
      await dongle.setEnable(true);
      await dongle.setMonitorAll(this.monitorAll);
      await this.refreshStatus();
    });
  }

  async disconnect(): Promise<void> {
    await this.dongle?.close();
    this.dropConnection('closed');
  }

  async readEdid(): Promise<void> {
    const dongle = this.requireDongle();
    const blocks: Uint8Array[] = [];
    try {
      blocks.push(await dongle.getEdidBlock(0));
      const total = Math.min(1 + extensionCount(blocks[0]), EDID_MAX_BLOCKS);
      while (blocks.length < total) {
        blocks.push(await dongle.getEdidBlock(blocks.length));
      }
    } catch {
      this.note('no EDID from the display');
      this.edidPhysAddr = null;
      return;
    }
    if (!blocks.every(checksumOk)) {
      this.error('EDID checksum mismatch');
      return;
    }
    const pa = edidPhysAddr(blocks);
    this.edidPhysAddr = pa;
    if (pa !== PHYS_ADDR_INVALID) {
      this.physAddr = pa;
      this.note(`EDID physical address ${formatPhysAddr(pa)}`);
    }
  }

  async refreshStatus(): Promise<void> {
    this.status = await this.requireDongle().getStatus();
  }

  async storePhysAddr(pa: number): Promise<void> {
    await this.run('store physical address', async () => {
      await this.requireDongle().storePhysAddr(pa);
      this.physAddr = pa;
    });
  }

  async setMonitorAll(on: boolean): Promise<void> {
    this.monitorAll = on;
    await this.dongle?.setMonitorAll(on);
  }

  async claimLogicalAddress(la: LogicalAddress): Promise<void> {
    await this.requireDongle().setAckMask(la === UNREGISTERED ? 0 : 1 << la);
    this.logicalAddress = la;
  }

  setActiveSource(active: boolean): void {
    this.activeSource = active;
  }

  async send(dest: LogicalAddress, opcode: Opcode, operands: number[] = []): Promise<TxStatus> {
    return this.transmit(buildFrame(this.logicalAddress, dest, opcode, operands));
  }

  async poll(dest: LogicalAddress, initiator: LogicalAddress = this.logicalAddress): Promise<TxStatus> {
    return this.transmit(buildFrame(initiator, dest));
  }

  waitFor(predicate: FramePredicate, timeoutMs: number): Promise<DecodedFrame | null> {
    return new Promise((resolve) => {
      const waiter: Waiter = {
        predicate,
        resolve: (frame) => {
          clearTimeout(timer);
          this.waiters.delete(waiter);
          resolve(frame);
        },
      };
      const timer = setTimeout(() => waiter.resolve(null), timeoutMs);
      this.waiters.add(waiter);
    });
  }

  // Runs one user action at a time and logs its failure.
  async run(label: string, action: () => Promise<unknown>): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = label;
    try {
      await action();
    } catch (err) {
      this.error(`${label}: ${err instanceof Error ? err.message : String(err)}`);
    } finally {
      this.busy = null;
    }
  }

  allocate(): Promise<void> {
    return this.run('allocate address', () => allocateLogicalAddress(this));
  }

  clearLog(): void {
    this.log = [];
  }

  note(text: string): void {
    this.append({ kind: LogKind.NOTE, text });
  }

  private error(text: string): void {
    this.append({ kind: LogKind.ERROR, text });
  }

  private async transmit(frame: Uint8Array): Promise<TxStatus> {
    const status = await this.requireDongle().transmit(frame);
    this.append({ kind: LogKind.TX, text: describeFrame(frame), bytes: toHex(frame), status: TX_STATUS_NAMES[status] });
    return status;
  }

  private onFrame(frame: Uint8Array): void {
    this.append({ kind: LogKind.RX, text: describeFrame(frame), bytes: toHex(frame) });
    const decoded = decodeFrame(frame);
    for (const waiter of this.waiters) {
      if (waiter.predicate(decoded)) {
        waiter.resolve(decoded);
      }
    }
    if (this.autoReply) {
      void this.reply(decoded).catch((err) => this.error(`auto-reply: ${err.message}`));
    }
  }

  // The minimum a TV expects from a source to keep it listed.
  private async reply(f: DecodedFrame): Promise<void> {
    const toUs = f.dest === this.logicalAddress && this.logicalAddress !== UNREGISTERED;
    const { BROADCAST } = LogicalAddress;
    if (f.dest === BROADCAST && f.opcode === Opcode.REQUEST_ACTIVE_SOURCE && this.activeSource) {
      await this.send(BROADCAST, Opcode.ACTIVE_SOURCE, physAddrBytes(this.physAddr));
      return;
    }
    if (f.dest === BROADCAST && f.opcode === Opcode.ACTIVE_SOURCE) {
      this.activeSource = false;
      return;
    }
    if (!toUs) {
      return;
    }
    switch (f.opcode) {
      case Opcode.GIVE_PHYSICAL_ADDRESS:
        await this.send(BROADCAST, Opcode.REPORT_PHYSICAL_ADDRESS, [...physAddrBytes(this.physAddr), this.deviceType]);
        break;
      case Opcode.GIVE_OSD_NAME:
        await this.send(f.initiator, Opcode.SET_OSD_NAME, Array.from(this.osdName, (c) => c.charCodeAt(0)));
        break;
      case Opcode.GET_CEC_VERSION:
        await this.send(f.initiator, Opcode.CEC_VERSION, [CecVersion.V1_4]);
        break;
      case Opcode.GIVE_DEVICE_POWER_STATUS:
        await this.send(f.initiator, Opcode.REPORT_POWER_STATUS, [PowerStatus.ON]);
        break;
    }
  }

  private append(entry: Omit<LogEntry, 'id' | 'time'>): void {
    this.log = [{ ...entry, id: crypto.randomUUID(), time: new Date() }, ...this.log].slice(0, LOG_LIMIT);
  }

  private requireDongle(): Dongle {
    if (!this.dongle) {
      throw new Error('not connected');
    }
    return this.dongle;
  }

  private dropConnection(reason: string): void {
    if (!this.dongle) {
      return;
    }
    this.dongle = null;
    this.info = null;
    this.status = null;
    this.logicalAddress = UNREGISTERED;
    this.activeSource = false;
    this.devices = [];
    for (const waiter of this.waiters) {
      waiter.resolve(null);
    }
    this.note(`disconnected: ${reason}`);
  }
}
