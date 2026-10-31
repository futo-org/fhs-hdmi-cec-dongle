<script lang="ts">
  import { Badge } from '$lib/components/ui/badge';
  import { Button } from '$lib/components/ui/button';
  import * as Card from '$lib/components/ui/card';
  import { ScrollArea } from '$lib/components/ui/scroll-area';
  import { LogKind, type CecSession } from '$lib/cec/session.svelte';
  import { cn } from '$lib/utils';

  let { session }: { session: CecSession } = $props();

  const KIND_CLASS: Record<LogKind, string> = {
    [LogKind.TX]: 'text-sky-600',
    [LogKind.RX]: 'text-emerald-600',
    [LogKind.NOTE]: 'text-muted-foreground',
    [LogKind.ERROR]: 'text-destructive',
  };

  const timeFormat = new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    fractionalSecondDigits: 3,
    hour12: false,
  });
</script>

<Card.Root class="flex h-full flex-col">
  <Card.Header>
    <Card.Title>Bus log</Card.Title>
    <Card.Action>
      <Button size="sm" variant="ghost" onclick={() => session.clearLog()}>Clear</Button>
    </Card.Action>
  </Card.Header>
  <Card.Content class="min-h-0 flex-1">
    <ScrollArea class="h-[calc(100vh-10rem)]">
      <ul class="grid gap-1 font-mono text-xs">
        {#each session.log as entry (entry.id)}
          <li class="grid grid-cols-[6.5rem_2rem_1fr] gap-2">
            <span class="text-muted-foreground">{timeFormat.format(entry.time)}</span>
            <span class={cn('uppercase', KIND_CLASS[entry.kind])}>{entry.kind}</span>
            <span>
              {entry.text}
              {#if entry.status}
                <Badge variant={entry.status === 'OK' ? 'secondary' : 'destructive'} class="ml-1">{entry.status}</Badge>
              {/if}
              {#if entry.bytes}
                <div class="text-muted-foreground">{entry.bytes}</div>
              {/if}
            </span>
          </li>
        {/each}
      </ul>
    </ScrollArea>
  </Card.Content>
</Card.Root>
