import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { TxStatus } from '$lib/usb/proto';
import type { CecBus } from './bus';
import { DeviceType, LogicalAddress } from './constants';
import { decodeFrame } from './frame';
import { REPLY_TIMEOUT_MS, query } from './macros';
import { Opcode } from './opcodes';
import { ReplyWaiters } from './replies';

const TV_POWER_ON = decodeFrame(Uint8Array.from([0x04, Opcode.REPORT_POWER_STATUS, 0x00]));

function fakeBus(replies: ReplyWaiters, send: CecBus['send']): CecBus {
  return {
    logicalAddress: LogicalAddress.PLAYBACK_1,
    physAddr: 0x1000,
    deviceType: DeviceType.PLAYBACK,
    send,
    poll: async () => TxStatus.OK,
    expectReply: (predicate) => replies.expect(predicate),
    claimLogicalAddress: async () => undefined,
    setActiveSource: () => undefined,
    note: () => undefined,
  };
}

function askTvPower(bus: CecBus) {
  return query(bus, LogicalAddress.TV, Opcode.GIVE_DEVICE_POWER_STATUS, Opcode.REPORT_POWER_STATUS);
}

describe('query', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('gives the reply its full window after a slow send', async () => {
    const replies = new ReplyWaiters();
    const slowSend = () => new Promise<TxStatus>((resolve) => setTimeout(() => resolve(TxStatus.OK), REPLY_TIMEOUT_MS));

    const result = askTvPower(fakeBus(replies, slowSend));
    await vi.advanceTimersByTimeAsync(REPLY_TIMEOUT_MS + REPLY_TIMEOUT_MS / 2);
    replies.deliver(TV_POWER_ON);

    expect(await result).toBe(TV_POWER_ON);
  });

  it('keeps a reply that arrives before the send resolves', async () => {
    const replies = new ReplyWaiters();
    const send = async () => {
      replies.deliver(TV_POWER_ON);
      return TxStatus.OK;
    };

    expect(await askTvPower(fakeBus(replies, send))).toBe(TV_POWER_ON);
  });

  it('is null when nobody answers', async () => {
    const result = askTvPower(fakeBus(new ReplyWaiters(), async () => TxStatus.OK));
    await vi.advanceTimersByTimeAsync(REPLY_TIMEOUT_MS);
    expect(await result).toBeNull();
  });
});
