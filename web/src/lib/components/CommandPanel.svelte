<script lang="ts">
  import { Button } from '$lib/components/ui/button';
  import * as Card from '$lib/components/ui/card';
  import * as Tabs from '$lib/components/ui/tabs';
  import { OPCODE_GROUPS } from '$lib/cec/opcodes';
  import type { CecSession } from '$lib/cec/session.svelte';
  import CommandRow from './CommandRow.svelte';
  import OptionSelect from './OptionSelect.svelte';
  import { DESTINATION_OPTIONS } from './options';

  let { session }: { session: CecSession } = $props();

  let pollDest = $state(0);
</script>

<Card.Root>
  <Card.Header>
    <Card.Title>Commands</Card.Title>
    <Card.Description>Every CEC opcode, sent from the current logical address.</Card.Description>
    <Card.Action class="flex items-center gap-2">
      <OptionSelect bind:value={pollDest} options={DESTINATION_OPTIONS} class="w-40" />
      <Button
        size="sm"
        variant="outline"
        disabled={!session.connected || !!session.busy}
        onclick={() => session.run('poll', () => session.poll(pollDest))}
      >
        Poll
      </Button>
    </Card.Action>
  </Card.Header>
  <Card.Content>
    <Tabs.Root value={OPCODE_GROUPS[0].group}>
      <Tabs.List class="flex h-auto flex-wrap">
        {#each OPCODE_GROUPS as { group } (group)}
          <Tabs.Trigger value={group}>{group}</Tabs.Trigger>
        {/each}
      </Tabs.List>
      {#each OPCODE_GROUPS as { group, specs } (group)}
        <Tabs.Content value={group}>
          {#each specs as spec (spec.opcode)}
            <CommandRow {session} {spec} />
          {/each}
        </Tabs.Content>
      {/each}
    </Tabs.Root>
  </Card.Content>
</Card.Root>
