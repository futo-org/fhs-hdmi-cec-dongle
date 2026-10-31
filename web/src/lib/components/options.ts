import { ALL_LOGICAL_ADDRESSES, LOGICAL_ADDRESS_NAMES } from '$lib/cec/constants';
import type { EnumOption } from '$lib/cec/opcodes';

export const DESTINATION_OPTIONS: EnumOption[] = ALL_LOGICAL_ADDRESSES.map((la) => ({
  value: la,
  label: `${la} · ${LOGICAL_ADDRESS_NAMES[la]}`,
}));
