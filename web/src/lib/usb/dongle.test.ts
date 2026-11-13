import { afterEach, describe, expect, it, vi } from 'vitest';
import { Dongle, TX_TIMEOUT_MS, type DongleListener } from './dongle';
import { INFO_SIZE, PROTO_MAJOR, Request, USB_CLASS, USB_PROTOCOL, USB_SUBCLASS } from './proto';

const EVENT_ENDPOINT = 1;
const FRAME = Uint8Array.from([0x40, 0x04]);

function never<T>(): Promise<T> {
  return new Promise(() => undefined);
}

function infoResponse(): USBInTransferResult {
  const data = new DataView(new ArrayBuffer(INFO_SIZE));
  data.setUint8(0, PROTO_MAJOR);
  return { status: 'ok', data };
}

function fakeDevice() {
  const itf = {
    interfaceNumber: 0,
    alternates: [{ interfaceClass: USB_CLASS, interfaceSubclass: USB_SUBCLASS, interfaceProtocol: USB_PROTOCOL }],
    alternate: { endpoints: [{ direction: 'in', type: 'interrupt', endpointNumber: EVENT_ENDPOINT }] },
  };
  return {
    configuration: { interfaces: [itf] },
    open: vi.fn(async () => undefined),
    close: vi.fn(async () => undefined),
    selectConfiguration: vi.fn(async () => undefined),
    claimInterface: vi.fn(async () => undefined),
    controlTransferIn: vi.fn(async (): Promise<USBInTransferResult> => infoResponse()),
    controlTransferOut: vi.fn(async (setup: USBControlTransferParameters): Promise<USBOutTransferResult> =>
      setup.request === Request.TRANSMIT ? never() : { status: 'ok', bytesWritten: 0 },
    ),
    transferIn: vi.fn((): Promise<USBInTransferResult> => never()),
  };
}

function listener(): DongleListener {
  return { onFrame: vi.fn(), onDisconnect: vi.fn() };
}

function open(device: ReturnType<typeof fakeDevice>, l = listener()) {
  return Dongle.open(device as unknown as USBDevice, l);
}

function disabled(device: ReturnType<typeof fakeDevice>): boolean {
  return device.controlTransferOut.mock.calls.some(([setup]) => setup.request === Request.SET_ENABLE && setup.value === 0);
}

afterEach(() => vi.useRealTimers());

describe('Dongle', () => {
  it('closes the device when GET_INFO fails', async () => {
    const device = fakeDevice();
    device.controlTransferIn.mockRejectedValue(new Error('stall'));

    await expect(open(device)).rejects.toThrow('stall');
    expect(device.close).toHaveBeenCalled();
  });

  it('times out a transmit whose control transfer never completes', async () => {
    vi.useFakeTimers();
    const dongle = await open(fakeDevice());

    const tx = expect(dongle.transmit(FRAME)).rejects.toThrow('timed out');
    await vi.advanceTimersByTimeAsync(TX_TIMEOUT_MS);
    await tx;
  });

  it('fails a pending transmit on close', async () => {
    const device = fakeDevice();
    const dongle = await open(device);

    const tx = expect(dongle.transmit(FRAME)).rejects.toThrow('closed');
    await new Promise((resolve) => setTimeout(resolve));
    await dongle.close();
    await tx;
    expect(device.close).toHaveBeenCalled();
  });

  it('releases the device once when the event read fails', async () => {
    const device = fakeDevice();
    device.transferIn.mockRejectedValueOnce(new Error('babble'));
    const l = listener();
    const dongle = await open(device, l);
    await new Promise((resolve) => setTimeout(resolve));

    expect(l.onDisconnect).toHaveBeenCalledOnce();
    expect(disabled(device)).toBe(true);
    expect(device.close).toHaveBeenCalledOnce();

    await dongle.close();
    expect(device.close).toHaveBeenCalledOnce();
  });
});
