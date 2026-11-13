import {
  EDID_BLOCK_SIZE,
  EVENT_PACKET_SIZE,
  EventType,
  INFO_SIZE,
  PROTO_MAJOR,
  Request,
  STATUS_SIZE,
  TX_BUDGET_MS,
  TxStatus,
  USB_CLASS,
  USB_PID,
  USB_PROTOCOL,
  USB_SUBCLASS,
  USB_VID,
  parseEvent,
  parseInfo,
  parseStatus,
  type Info,
  type Status,
} from './proto';

export const TX_TIMEOUT_MS = TX_BUDGET_MS + 500;
const SEQ_MODULO = 0x100;

export class DongleError extends Error {}

interface PendingTx {
  seq: number;
  resolve: (status: TxStatus) => void;
  reject: (err: Error) => void;
}

export interface DongleListener {
  onFrame(frame: Uint8Array): void;
  onDisconnect(reason: string): void;
}

// One adapter over WebUSB: vendor control requests out, events from the interrupt IN endpoint.
export class Dongle {
  private nextSeq = 0;
  private pending: PendingTx | null = null;
  private txChain: Promise<unknown> = Promise.resolve();
  private stopped: Promise<void> | null = null;

  private constructor(
    private readonly device: USBDevice,
    private readonly ifnum: number,
    private readonly eventEndpoint: number,
    private readonly listener: DongleListener,
  ) {}

  static isSupported(): boolean {
    return typeof navigator !== 'undefined' && 'usb' in navigator;
  }

  static async request(listener: DongleListener): Promise<Dongle> {
    const device = await navigator.usb.requestDevice({
      filters: [{ vendorId: USB_VID, productId: USB_PID }],
    });
    return Dongle.open(device, listener);
  }

  static async open(device: USBDevice, listener: DongleListener): Promise<Dongle> {
    await device.open();
    try {
      const dongle = await Dongle.attach(device, listener);
      void dongle.readEvents();
      return dongle;
    } catch (err) {
      await device.close().catch(() => undefined);
      throw err;
    }
  }

  private static async attach(device: USBDevice, listener: DongleListener): Promise<Dongle> {
    if (device.configuration === null) {
      await device.selectConfiguration(1);
    }
    const itf = device.configuration?.interfaces.find((i) =>
      i.alternates.some(
        (a) =>
          a.interfaceClass === USB_CLASS &&
          a.interfaceSubclass === USB_SUBCLASS &&
          a.interfaceProtocol === USB_PROTOCOL,
      ),
    );
    if (!itf) {
      throw new DongleError('no CEC interface on this device');
    }
    await device.claimInterface(itf.interfaceNumber);
    const endpoint = itf.alternate.endpoints.find((e) => e.direction === 'in' && e.type === 'interrupt');
    if (!endpoint) {
      throw new DongleError('CEC interface has no event endpoint');
    }

    const dongle = new Dongle(device, itf.interfaceNumber, endpoint.endpointNumber, listener);
    const info = await dongle.getInfo();
    if (info.protoMajor !== PROTO_MAJOR) {
      throw new DongleError(`protocol ${info.protoMajor}.x, expected ${PROTO_MAJOR}.x`);
    }
    return dongle;
  }

  get name(): string {
    return `${this.device.productName ?? 'dongle'} ${this.device.serialNumber ?? ''}`.trim();
  }

  owns(device: USBDevice): boolean {
    return device === this.device;
  }

  close(): Promise<void> {
    return this.shutdown('closed');
  }

  private shutdown(reason: string): Promise<void> {
    this.stopped ??= this.release(reason);
    return this.stopped;
  }

  private async release(reason: string): Promise<void> {
    this.failPending(reason);
    await this.setEnable(false).catch(() => undefined);
    await this.device.close().catch(() => undefined);
  }

  async getInfo(): Promise<Info> {
    return parseInfo(await this.controlIn(Request.GET_INFO, 0, INFO_SIZE));
  }

  async getStatus(): Promise<Status> {
    return parseStatus(await this.controlIn(Request.GET_STATUS, 0, STATUS_SIZE));
  }

  // Stalls when the display does not answer over DDC.
  async getEdidBlock(block: number): Promise<Uint8Array> {
    const view = await this.controlIn(Request.GET_EDID, block, EDID_BLOCK_SIZE);
    return new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
  }

  setEnable(on: boolean): Promise<void> {
    return this.controlOut(Request.SET_ENABLE, on ? 1 : 0);
  }

  setAckMask(mask: number): Promise<void> {
    return this.controlOut(Request.SET_LOG_ADDRS, mask);
  }

  setMonitorAll(on: boolean): Promise<void> {
    return this.controlOut(Request.SET_MONITOR_ALL, on ? 1 : 0);
  }

  storePhysAddr(pa: number): Promise<void> {
    return this.controlOut(Request.SET_PHYS_ADDR, pa);
  }

  // The firmware holds one frame at a time, so transmits run in order.
  transmit(frame: Uint8Array): Promise<TxStatus> {
    const run = this.txChain.then(() => this.transmitNow(frame));
    this.txChain = run.catch(() => undefined);
    return run;
  }

  private async transmitNow(frame: Uint8Array): Promise<TxStatus> {
    const seq = this.nextSeq;
    this.nextSeq = (this.nextSeq + 1) % SEQ_MODULO;

    const done = new Promise<TxStatus>((resolve, reject) => {
      this.pending = { seq, resolve, reject };
    });
    const timeout = setTimeout(() => this.failPending('transmit timed out'), TX_TIMEOUT_MS);
    try {
      const [, status] = await Promise.all([this.controlOut(Request.TRANSMIT, seq, frame), done]);
      return status;
    } finally {
      clearTimeout(timeout);
      if (this.pending?.seq === seq) {
        this.pending = null;
      }
    }
  }

  private failPending(reason: string): void {
    this.pending?.reject(new DongleError(reason));
    this.pending = null;
  }

  private async readEvents(): Promise<void> {
    while (!this.stopped) {
      let result: USBInTransferResult;
      try {
        result = await this.device.transferIn(this.eventEndpoint, EVENT_PACKET_SIZE);
      } catch (err) {
        if (!this.stopped) {
          void this.shutdown('disconnected');
          this.listener.onDisconnect(err instanceof Error ? err.message : String(err));
        }
        return;
      }
      if (result.status !== 'ok' || !result.data) {
        continue;
      }
      const event = parseEvent(result.data);
      if (event?.type === EventType.TX_DONE && this.pending?.seq === event.seq) {
        this.pending.resolve(event.status);
        this.pending = null;
      } else if (event?.type === EventType.RX) {
        this.listener.onFrame(event.frame);
      }
    }
  }

  private async controlOut(request: Request, value: number, data?: Uint8Array): Promise<void> {
    const setup: USBControlTransferParameters = {
      requestType: 'vendor',
      recipient: 'interface',
      request,
      value,
      index: this.ifnum,
    };
    const result = await this.device.controlTransferOut(setup, data as BufferSource | undefined);
    if (result.status !== 'ok') {
      throw new DongleError(`request ${Request[request]} ${result.status}`);
    }
  }

  private async controlIn(request: Request, value: number, length: number): Promise<DataView> {
    const result = await this.device.controlTransferIn(
      { requestType: 'vendor', recipient: 'interface', request, value, index: this.ifnum },
      length,
    );
    if (result.status !== 'ok' || !result.data || result.data.byteLength < length) {
      throw new DongleError(`request ${Request[request]} ${result.status}`);
    }
    return result.data;
  }
}
