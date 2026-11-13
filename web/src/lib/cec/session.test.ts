import { afterEach, describe, expect, it, vi } from 'vitest';
import { Dongle, type DongleListener } from '$lib/usb/dongle';
import { STATUS_FIELDS, TxStatus, type Info } from '$lib/usb/proto';
import { LogicalAddress } from './constants';
import { Opcode } from './opcodes';
import { CecSession, LogKind } from './session.svelte';

const INFO: Info = { protoMajor: 1, protoMinor: 0, fwVersion: '0.0.0', physAddr: 0x1000, caps: 0 };
const TV_TO_ALL = 0x0f;
const TV_TO_PLAYBACK_1 = 0x04;
const PLAYBACK_1_TO_ALL = 0x4f;
const PLAYBACK_1_TO_TV = 0x40;

function fakeDongle() {
  return {
    name: 'fake',
    getInfo: vi.fn(async () => INFO),
    getStatus: vi.fn(async () => Object.fromEntries(STATUS_FIELDS.map((f) => [f, 0]))),
    setEnable: vi.fn(async () => undefined),
    setMonitorAll: vi.fn(async () => undefined),
    setAckMask: vi.fn(async () => undefined),
    storePhysAddr: vi.fn(async () => undefined),
    transmit: vi.fn(async (_frame: Uint8Array) => TxStatus.OK),
    close: vi.fn(async () => undefined),
    owns: () => false,
  };
}

async function connected(dongle = fakeDongle()) {
  let listener: DongleListener | undefined;
  vi.spyOn(Dongle, 'request').mockImplementation(async (l) => {
    listener = l;
    return dongle as unknown as Dongle;
  });
  const session = new CecSession();
  await session.connect();
  const receive = async (bytes: number[]) => {
    listener!.onFrame(Uint8Array.from(bytes));
    await new Promise((resolve) => setTimeout(resolve));
  };
  return { session, dongle, receive };
}

function sentFrames(dongle: ReturnType<typeof fakeDongle>): number[][] {
  return dongle.transmit.mock.calls.map(([frame]) => Array.from(frame));
}

afterEach(() => vi.restoreAllMocks());

describe('CecSession', () => {
  it('closes the dongle when initialization fails', async () => {
    const dongle = fakeDongle();
    dongle.setEnable.mockRejectedValue(new Error('stall'));
    const { session } = await connected(dongle);

    expect(dongle.close).toHaveBeenCalled();
    expect(session.connected).toBe(false);
    expect(session.log.some((e) => e.kind === LogKind.ERROR && e.text.includes('stall'))).toBe(true);
  });

  it('stores the physical address from inside another action', async () => {
    const { session, dongle } = await connected();
    await session.run('physical address', () => session.storePhysAddr(0x2000));

    expect(dongle.storePhysAddr).toHaveBeenCalledWith(0x2000);
    expect(session.physAddr).toBe(0x2000);
  });

  it('restores monitor-all and logs when the dongle rejects it', async () => {
    const { session, dongle } = await connected();
    dongle.setMonitorAll.mockRejectedValue(new Error('stall'));
    await session.run('monitor all', () => session.setMonitorAll(true));

    expect(session.monitorAll).toBe(false);
    expect(session.log[0].kind).toBe(LogKind.ERROR);
  });

  it('becomes the active source when the TV sets the stream path to it', async () => {
    const { session, dongle, receive } = await connected();
    await session.claimLogicalAddress(LogicalAddress.PLAYBACK_1);

    await receive([TV_TO_ALL, Opcode.SET_STREAM_PATH, 0x20, 0x00]);
    expect(dongle.transmit).not.toHaveBeenCalled();

    await receive([TV_TO_ALL, Opcode.SET_STREAM_PATH, 0x10, 0x00]);
    expect(sentFrames(dongle)).toEqual([[PLAYBACK_1_TO_ALL, Opcode.ACTIVE_SOURCE, 0x10, 0x00]]);
    expect(session.activeSource).toBe(true);
  });

  it('answers Give OSD Name with the ASCII name', async () => {
    const { session, dongle, receive } = await connected();
    await session.claimLogicalAddress(LogicalAddress.PLAYBACK_1);
    session.setOsdName('Bench');

    await receive([TV_TO_PLAYBACK_1, Opcode.GIVE_OSD_NAME]);
    expect(sentFrames(dongle)).toEqual([[PLAYBACK_1_TO_TV, Opcode.SET_OSD_NAME, ...Array.from('Bench', (c) => c.charCodeAt(0))]]);
  });

  it('rejects a non-ASCII OSD name before it reaches the bus', async () => {
    const { session, dongle, receive } = await connected();
    await session.claimLogicalAddress(LogicalAddress.PLAYBACK_1);

    expect(() => session.setOsdName('Bänch')).toThrow();
    session.osdName = 'Bänch';
    await receive([TV_TO_PLAYBACK_1, Opcode.GIVE_OSD_NAME]);

    expect(dongle.transmit).not.toHaveBeenCalled();
    expect(session.log[0].kind).toBe(LogKind.ERROR);
  });
});
