import { describe, expect, it } from 'vitest';
import { OperandKind, type OperandSpec } from '$lib/cec/opcodes';
import { PHYS_ADDR_INVALID } from '$lib/usb/proto';
import { CommandDraft } from './command-draft.svelte';

const ROUTING_CHANGE: OperandSpec[] = [
  { kind: OperandKind.PHYS_ADDR, name: 'Original address' },
  { kind: OperandKind.PHYS_ADDR, name: 'New address' },
];

describe('CommandDraft', () => {
  it('follows the session address until a field is edited', () => {
    const draft = new CommandDraft(ROUTING_CHANGE, PHYS_ADDR_INVALID);
    draft.edit(1, '2.0.0.0');

    draft.followPhysAddr(0x1000);

    expect(draft.values).toEqual(['1.0.0.0', '2.0.0.0']);
    expect(draft.encode()).toEqual([0x10, 0x00, 0x20, 0x00]);
  });
});
