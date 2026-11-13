<script lang="ts">
  import { Button } from '$lib/components/ui/button';
  import * as Card from '$lib/components/ui/card';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { LogicalAddress, UiCommand } from '$lib/cec/constants';
  import { formatPhysAddr, parsePhysAddr } from '$lib/cec/frame';
  import {
    allocateLogicalAddress,
    inactiveSource,
    oneTouchPlay,
    powerStatus,
    pressKey,
    scanBus,
    standby,
    switchInput,
    systemAudioOn,
    wakeAndWait,
  } from '$lib/cec/macros';
  import { UI_COMMAND_OPTIONS } from '$lib/cec/opcodes';
  import type { CecSession } from '$lib/cec/session.svelte';
  import OptionSelect from './OptionSelect.svelte';
  import { DESTINATION_OPTIONS } from './options';

  let { session }: { session: CecSession } = $props();

  const uid = $props.id();
  const ids = { input: `${uid}-input`, remoteDest: `${uid}-remote-dest` };

  const WAKE_TIMEOUT_MS = 20_000;
  const WAKE_POLL_MS = 1_000;

  const KEYPAD: { key: UiCommand; label: string }[] = [
    { key: UiCommand.POWER_TOGGLE, label: 'Power' },
    { key: UiCommand.UP, label: '▲' },
    { key: UiCommand.ROOT_MENU, label: 'Menu' },
    { key: UiCommand.LEFT, label: '◀' },
    { key: UiCommand.SELECT, label: 'OK' },
    { key: UiCommand.RIGHT, label: '▶' },
    { key: UiCommand.BACK, label: 'Back' },
    { key: UiCommand.DOWN, label: '▼' },
    { key: UiCommand.INPUT_SELECT, label: 'Input' },
    { key: UiCommand.VOLUME_DOWN, label: 'Vol −' },
    { key: UiCommand.MUTE, label: 'Mute' },
    { key: UiCommand.VOLUME_UP, label: 'Vol +' },
    { key: UiCommand.REWIND, label: '⏪' },
    { key: UiCommand.PLAY, label: '▶︎' },
    { key: UiCommand.FAST_FORWARD, label: '⏩' },
    { key: UiCommand.PAUSE, label: '⏸' },
    { key: UiCommand.STOP, label: '⏹' },
    { key: UiCommand.DISPLAY_INFO, label: 'Info' },
  ];

  let remoteDest = $state(LogicalAddress.TV);
  let otherKey = $state(UiCommand.NUMBER_1);
  let inputText = $state('');
  $effect(() => {
    inputText = formatPhysAddr(session.physAddr);
  });

  const disabled = $derived(!session.connected || !!session.busy);

  const steps: { label: string; detail: string; action: () => Promise<unknown> }[] = [
    { label: 'Allocate address', detail: 'Poll for a free playback address, claim it, report it', action: () => allocateLogicalAddress(session) },
    { label: 'One Touch Play', detail: 'Image View On → TV, Active Source → all', action: () => oneTouchPlay(session) },
    {
      label: 'Wake TV and wait',
      detail: 'One Touch Play, then poll power status until on',
      action: async () =>
        session.note((await wakeAndWait(session, WAKE_TIMEOUT_MS, WAKE_POLL_MS)) ? 'TV is on' : 'TV did not wake'),
    },
    { label: 'Inactive Source', detail: 'Tell the TV we stopped', action: () => inactiveSource(session) },
    { label: 'Standby TV', detail: 'Standby → TV', action: () => standby(session, LogicalAddress.TV) },
    { label: 'Standby all', detail: 'Standby → broadcast', action: () => standby(session, LogicalAddress.BROADCAST) },
    { label: 'TV power status', detail: 'Ask the TV and wait for the reply', action: () => powerStatus(session, LogicalAddress.TV) },
    { label: 'System audio on', detail: 'Ask the audio system to take over volume', action: () => systemAudioOn(session) },
    {
      label: 'Scan bus',
      detail: 'Poll every address, query whoever answers',
      action: () => {
        session.devices = [];
        return scanBus(session, (d) => (session.devices = [...session.devices, d]));
      },
    },
  ];

  function runStep(label: string, action: () => Promise<unknown>) {
    void session.run(label, action);
  }
</script>

<Card.Root>
  <Card.Header>
    <Card.Title>Procedures</Card.Title>
    <Card.Description>Multi-step sequences. Allocate an address first.</Card.Description>
  </Card.Header>
  <Card.Content class="grid gap-6">
    <div class="grid grid-cols-3 gap-2">
      {#each steps as step (step.label)}
        <Button variant="outline" class="h-auto flex-col items-start py-2 text-left" {disabled} onclick={() => runStep(step.label, step.action)}>
          <span>{step.label}</span>
          <span class="text-muted-foreground text-xs font-normal whitespace-normal">{step.detail}</span>
        </Button>
      {/each}
    </div>

    <div class="flex items-center gap-2">
      <Label for={ids.input}>Switch TV input to</Label>
      <Input id={ids.input} class="h-8 w-28 font-mono" bind:value={inputText} />
      <Button size="sm" variant="outline" {disabled} onclick={() => runStep('switch input', () => switchInput(session, parsePhysAddr(inputText)))}>
        Set Stream Path
      </Button>
    </div>

    <div class="grid gap-3">
      <div class="flex items-center gap-2">
        <Label for={ids.remoteDest}>Remote to</Label>
        <OptionSelect id={ids.remoteDest} bind:value={remoteDest} options={DESTINATION_OPTIONS} />
      </div>
      <div class="grid w-72 grid-cols-3 gap-2">
        {#each KEYPAD as { key, label } (key)}
          <Button size="sm" variant="secondary" {disabled} onclick={() => runStep('key', () => pressKey(session, remoteDest, key))}>
            {label}
          </Button>
        {/each}
      </div>
      <div class="flex items-center gap-2">
        <OptionSelect bind:value={otherKey} options={UI_COMMAND_OPTIONS} />
        <Button size="sm" variant="secondary" {disabled} onclick={() => runStep('key', () => pressKey(session, remoteDest, otherKey))}>
          Press
        </Button>
      </div>
    </div>
  </Card.Content>
</Card.Root>
