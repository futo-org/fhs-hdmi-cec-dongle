<script lang="ts">
  import * as Select from '$lib/components/ui/select';
  import type { EnumOption } from '$lib/cec/opcodes';

  let {
    value = $bindable(),
    options,
    id,
    class: className = 'w-44',
  }: { value: number; options: EnumOption[]; id?: string; class?: string } = $props();

  const label = $derived(options.find((o) => o.value === value)?.label ?? '');
</script>

<Select.Root type="single" bind:value={() => String(value), (v) => (value = Number(v))}>
  <Select.Trigger {id} size="sm" class={className}>{label}</Select.Trigger>
  <Select.Content>
    {#each options as option (option.value)}
      <Select.Item value={String(option.value)} label={option.label} />
    {/each}
  </Select.Content>
</Select.Root>
