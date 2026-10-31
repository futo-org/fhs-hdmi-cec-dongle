<script lang="ts">
  import * as Card from '$lib/components/ui/card';
  import * as Table from '$lib/components/ui/table';
  import { LOGICAL_ADDRESS_NAMES } from '$lib/cec/constants';
  import type { CecSession } from '$lib/cec/session.svelte';

  let { session }: { session: CecSession } = $props();
</script>

{#if session.devices.length}
  <Card.Root>
    <Card.Header>
      <Card.Title>Devices on the bus</Card.Title>
    </Card.Header>
    <Card.Content>
      <Table.Root>
        <Table.Header>
          <Table.Row>
            <Table.Head>Address</Table.Head>
            <Table.Head>Physical</Table.Head>
            <Table.Head>Name</Table.Head>
            <Table.Head>Vendor</Table.Head>
            <Table.Head>CEC</Table.Head>
            <Table.Head>Power</Table.Head>
          </Table.Row>
        </Table.Header>
        <Table.Body>
          {#each session.devices as d (d.la)}
            <Table.Row>
              <Table.Cell>{d.la} · {LOGICAL_ADDRESS_NAMES[d.la]}</Table.Cell>
              <Table.Cell class="font-mono">{d.physAddr ?? '—'}</Table.Cell>
              <Table.Cell>{d.osdName ?? '—'}</Table.Cell>
              <Table.Cell class="font-mono">{d.vendorId ?? '—'}</Table.Cell>
              <Table.Cell>{d.cecVersion ?? '—'}</Table.Cell>
              <Table.Cell>{d.power ?? '—'}</Table.Cell>
            </Table.Row>
          {/each}
        </Table.Body>
      </Table.Root>
    </Card.Content>
  </Card.Root>
{/if}
