<script lang="ts">
  import CommandPanel from '$lib/components/CommandPanel.svelte';
  import ConnectionPanel from '$lib/components/ConnectionPanel.svelte';
  import DevicePanel from '$lib/components/DevicePanel.svelte';
  import LogPanel from '$lib/components/LogPanel.svelte';
  import MacroPanel from '$lib/components/MacroPanel.svelte';
  import { CecSession } from '$lib/cec/session.svelte';

  const session = new CecSession();
</script>

<main class="grid min-h-screen grid-cols-[minmax(0,1fr)_28rem] gap-4 p-4">
  <div class="grid content-start gap-4">
    <h1 class="text-xl font-semibold">FHS CEC dongle</h1>
    <ConnectionPanel {session} />
    <MacroPanel {session} />
    <DevicePanel {session} />
    <CommandPanel {session} />
  </div>
  <div class="sticky top-4 h-[calc(100vh-2rem)]">
    <LogPanel {session} />
  </div>
</main>
