<script lang="ts">
  import { Badge } from '$lib/components/ui/badge';
  import { Button } from '$lib/components/ui/button';
  import * as Card from '$lib/components/ui/card';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { Switch } from '$lib/components/ui/switch';
  import { DEVICE_TYPE_NAMES, LOGICAL_ADDRESS_NAMES, OSD_NAME_MAX } from '$lib/cec/constants';
  import { formatPhysAddr, parsePhysAddr } from '$lib/cec/frame';
  import type { CecSession } from '$lib/cec/session.svelte';
  import { PHYS_ADDR_INVALID, STATUS_FIELDS } from '$lib/usb/proto';

  let { session }: { session: CecSession } = $props();

  let physAddrText = $state('');
  $effect(() => {
    physAddrText = formatPhysAddr(session.physAddr);
  });

  function applyPhysAddr(store: boolean) {
    void session.run('physical address', async () => {
      const pa = parsePhysAddr(physAddrText);
      if (store) {
        await session.storePhysAddr(pa);
      } else {
        session.physAddr = pa;
      }
    });
  }
</script>

<Card.Root>
  <Card.Header>
    <Card.Title class="flex items-center gap-2">
      Dongle
      {#if session.connected}
        <Badge>connected</Badge>
      {:else}
        <Badge variant="secondary">disconnected</Badge>
      {/if}
      {#if session.busy}
        <Badge variant="outline">{session.busy}…</Badge>
      {/if}
    </Card.Title>
    <Card.Description>
      {#if session.info}
        firmware {session.info.fwVersion} · protocol {session.info.protoMajor}.{session.info.protoMinor}
      {:else if !session.supported}
        WebUSB is unavailable. Use a Chromium browser over https or localhost.
      {:else}
        On Linux, unbind the fhs-cec kernel driver first.
      {/if}
    </Card.Description>
    <Card.Action>
      {#if session.connected}
        <Button variant="outline" onclick={() => session.disconnect()}>Disconnect</Button>
      {:else}
        <Button disabled={!session.supported} onclick={() => session.connect()}>Connect</Button>
      {/if}
    </Card.Action>
  </Card.Header>

  {#if session.connected}
    <Card.Content class="grid gap-4">
      <div class="grid grid-cols-[8rem_1fr] items-center gap-x-3 gap-y-2 text-sm">
        <Label>Physical address</Label>
        <div class="flex flex-wrap items-center gap-2">
          <Input class="h-8 w-28 font-mono" bind:value={physAddrText} />
          <Button size="sm" variant="outline" onclick={() => applyPhysAddr(false)}>Use</Button>
          <Button size="sm" variant="outline" onclick={() => applyPhysAddr(true)}>Store in flash</Button>
          {#if session.hasEdid}
            <Button size="sm" variant="outline" onclick={() => session.run('read EDID', () => session.readEdid())}>
              Read EDID
            </Button>
          {/if}
          {#if session.edidPhysAddr !== null && session.edidPhysAddr !== PHYS_ADDR_INVALID}
            <span class="text-muted-foreground">EDID {formatPhysAddr(session.edidPhysAddr)}</span>
          {/if}
        </div>

        <Label>Logical address</Label>
        <div class="flex items-center gap-2">
          <span class="font-mono">{session.logicalAddress} · {LOGICAL_ADDRESS_NAMES[session.logicalAddress]}</span>
          <span class="text-muted-foreground">as {DEVICE_TYPE_NAMES[session.deviceType]}</span>
          <Button size="sm" variant="outline" disabled={!!session.busy} onclick={() => session.allocate()}>
            Allocate
          </Button>
        </div>

        <Label>OSD name</Label>
        <Input class="h-8 w-40" maxlength={OSD_NAME_MAX} bind:value={session.osdName} />

        <Label>Monitor all</Label>
        <Switch checked={session.monitorAll} onCheckedChange={(on) => session.setMonitorAll(on)} />

        <Label>Auto-reply</Label>
        <div class="flex items-center gap-2">
          <Switch bind:checked={session.autoReply} />
          <span class="text-muted-foreground">physical address, OSD name, version, power, active source</span>
        </div>
      </div>

      {#if session.status}
        <div class="grid grid-cols-5 gap-2 text-xs">
          {#each STATUS_FIELDS as field (field)}
            <div class="rounded-md border p-2">
              <div class="text-muted-foreground">{field}</div>
              <div class="font-mono text-sm">{session.status[field]}</div>
            </div>
          {/each}
        </div>
        <div>
          <Button size="sm" variant="ghost" onclick={() => session.run('status', () => session.refreshStatus())}>
            Refresh counters
          </Button>
        </div>
      {/if}
    </Card.Content>
  {/if}
</Card.Root>
