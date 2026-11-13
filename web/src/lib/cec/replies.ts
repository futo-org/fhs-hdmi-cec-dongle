import type { DecodedFrame } from './frame';

export type FramePredicate = (frame: DecodedFrame) => boolean;

// Catches a reply from the moment it is created. The timeout only starts at arm(),
// so time spent queued or on the wire does not eat into the reply window.
export interface PendingReply {
  arm(timeoutMs: number): Promise<DecodedFrame | null>;
  cancel(): void;
}

class Waiter implements PendingReply {
  private settled = false;
  private frame: DecodedFrame | null = null;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private resolve: ((frame: DecodedFrame | null) => void) | undefined;

  constructor(
    private readonly predicate: FramePredicate,
    private readonly onSettled: (waiter: Waiter) => void,
  ) {}

  offer(frame: DecodedFrame): void {
    if (this.predicate(frame)) {
      this.settle(frame);
    }
  }

  arm(timeoutMs: number): Promise<DecodedFrame | null> {
    return new Promise((resolve) => {
      if (this.settled) {
        resolve(this.frame);
        return;
      }
      this.resolve = resolve;
      this.timer = setTimeout(() => this.settle(null), timeoutMs);
    });
  }

  cancel(): void {
    this.settle(null);
  }

  private settle(frame: DecodedFrame | null): void {
    if (this.settled) {
      return;
    }
    this.settled = true;
    this.frame = frame;
    clearTimeout(this.timer);
    this.onSettled(this);
    this.resolve?.(frame);
  }
}

export class ReplyWaiters {
  private readonly waiters = new Set<Waiter>();

  expect(predicate: FramePredicate): PendingReply {
    const waiter = new Waiter(predicate, (w) => this.waiters.delete(w));
    this.waiters.add(waiter);
    return waiter;
  }

  deliver(frame: DecodedFrame): void {
    for (const waiter of this.waiters) {
      waiter.offer(frame);
    }
  }

  cancelAll(): void {
    for (const waiter of this.waiters) {
      waiter.cancel();
    }
  }
}
