<script lang="ts">
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { OperandKind, type OpcodeSpec } from '$lib/cec/opcodes';
  import type { CecSession } from '$lib/cec/session.svelte';
  import { CommandDraft } from './command-draft.svelte';
  import OptionSelect from './OptionSelect.svelte';
  import { DESTINATION_OPTIONS } from './options';

  let { session, spec }: { session: CecSession; spec: OpcodeSpec } = $props();

  // svelte-ignore state_referenced_locally
  let dest = $state(spec.defaultDest);
  // svelte-ignore state_referenced_locally
  const draft = new CommandDraft(spec.operands, session.physAddr);

  $effect(() => {
    draft.followPhysAddr(session.physAddr);
  });

  function send() {
    void session.run(spec.name, async () => {
      await session.send(dest, spec.opcode, draft.encode());
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
        bind:value={() => Number(draft.values[i]), (v) => draft.edit(i, String(v))}
        options={operand.options}
      />
    {:else}
      <Input
        class="h-8 w-44 font-mono"
        placeholder={operand.name}
        title={operand.name}
        maxlength={operand.kind === OperandKind.ASCII ? operand.maxLength : undefined}
        bind:value={() => draft.values[i], (v) => draft.edit(i, v)}
      />
    {/if}
  {/each}
  <Button size="sm" class="ml-auto" disabled={!session.connected || !!session.busy} onclick={send}>Send</Button>
</div>
