<script lang="ts">
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { encodeOperand, formatPhysAddr } from '$lib/cec/frame';
  import { OperandKind, type OpcodeSpec, type OperandSpec } from '$lib/cec/opcodes';
  import type { CecSession } from '$lib/cec/session.svelte';
  import OptionSelect from './OptionSelect.svelte';
  import { DESTINATION_OPTIONS } from './options';

  let { session, spec }: { session: CecSession; spec: OpcodeSpec } = $props();

  function initialValue(operand: OperandSpec): string {
    switch (operand.kind) {
      case OperandKind.ENUM:
        return String(operand.options[0].value);
      case OperandKind.PHYS_ADDR:
        return formatPhysAddr(session.physAddr);
      case OperandKind.BYTE:
        return '00';
      case OperandKind.VENDOR_ID:
        return '000000';
      default:
        return '';
    }
  }

  // svelte-ignore state_referenced_locally
  let dest = $state(spec.defaultDest);
  // svelte-ignore state_referenced_locally
  let values = $state(spec.operands.map(initialValue));

  function send() {
    void session.run(spec.name, async () => {
      const operands = spec.operands.flatMap((operand, i) => encodeOperand(operand, values[i]));
      await session.send(dest, spec.opcode, operands);
    });
  }
</script>

<div class="flex flex-wrap items-center gap-2 border-b py-2 last:border-b-0">
  <div class="w-56">
    <div class="text-sm font-medium">{spec.name}</div>
    <div class="text-muted-foreground font-mono text-xs">0x{spec.opcode.toString(16).padStart(2, '0')}</div>
  </div>
  <OptionSelect bind:value={dest} options={DESTINATION_OPTIONS} class="w-40" />
  {#each spec.operands as operand, i (operand.name)}
    {#if operand.kind === OperandKind.ENUM}
      <OptionSelect
        bind:value={() => Number(values[i]), (v) => (values[i] = String(v))}
        options={operand.options}
      />
    {:else}
      <Input
        class="h-8 w-44 font-mono"
        placeholder={operand.name}
        title={operand.name}
        maxlength={operand.kind === OperandKind.ASCII ? operand.maxLength : undefined}
        bind:value={values[i]}
      />
    {/if}
  {/each}
  <Button size="sm" class="ml-auto" disabled={!session.connected || !!session.busy} onclick={send}>Send</Button>
</div>
