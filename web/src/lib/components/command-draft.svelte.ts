import { encodeOperand, formatPhysAddr } from '$lib/cec/frame';
import { OperandKind, type OperandSpec } from '$lib/cec/opcodes';

function initialValue(operand: OperandSpec, physAddr: number): string {
  switch (operand.kind) {
    case OperandKind.ENUM:
      return String(operand.options[0].value);
    case OperandKind.PHYS_ADDR:
      return formatPhysAddr(physAddr);
    case OperandKind.BYTE:
      return '00';
    case OperandKind.VENDOR_ID:
      return '000000';
    default:
      return '';
  }
}

// Operand text for one command row. Physical-address fields follow the session until edited.
export class CommandDraft {
  values = $state<string[]>([]);
  private readonly edited: Set<number> = new Set();

  constructor(
    private readonly operands: OperandSpec[],
    physAddr: number,
  ) {
    this.values = operands.map((operand) => initialValue(operand, physAddr));
  }

  edit(index: number, value: string): void {
    this.values[index] = value;
    this.edited.add(index);
  }

  followPhysAddr(physAddr: number): void {
    this.operands.forEach((operand, i) => {
      if (operand.kind === OperandKind.PHYS_ADDR && !this.edited.has(i)) {
        this.values[i] = formatPhysAddr(physAddr);
      }
    });
  }

  encode(): number[] {
    return this.operands.flatMap((operand, i) => encodeOperand(operand, this.values[i]));
  }
}
