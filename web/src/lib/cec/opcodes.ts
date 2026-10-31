import { CecVersion, DEVICE_TYPE_NAMES, LogicalAddress, OSD_NAME_MAX, PowerStatus, UiCommand } from './constants';

export enum Opcode {
  FEATURE_ABORT = 0x00,
  IMAGE_VIEW_ON = 0x04,
  TUNER_STEP_INCREMENT = 0x05,
  TUNER_STEP_DECREMENT = 0x06,
  TUNER_DEVICE_STATUS = 0x07,
  GIVE_TUNER_DEVICE_STATUS = 0x08,
  RECORD_ON = 0x09,
  RECORD_STATUS = 0x0a,
  RECORD_OFF = 0x0b,
  TEXT_VIEW_ON = 0x0d,
  RECORD_TV_SCREEN = 0x0f,
  GIVE_DECK_STATUS = 0x1a,
  DECK_STATUS = 0x1b,
  SET_MENU_LANGUAGE = 0x32,
  CLEAR_ANALOGUE_TIMER = 0x33,
  SET_ANALOGUE_TIMER = 0x34,
  TIMER_STATUS = 0x35,
  STANDBY = 0x36,
  PLAY = 0x41,
  DECK_CONTROL = 0x42,
  TIMER_CLEARED_STATUS = 0x43,
  USER_CONTROL_PRESSED = 0x44,
  USER_CONTROL_RELEASED = 0x45,
  GIVE_OSD_NAME = 0x46,
  SET_OSD_NAME = 0x47,
  SET_OSD_STRING = 0x64,
  SET_TIMER_PROGRAM_TITLE = 0x67,
  SYSTEM_AUDIO_MODE_REQUEST = 0x70,
  GIVE_AUDIO_STATUS = 0x71,
  SET_SYSTEM_AUDIO_MODE = 0x72,
  REPORT_AUDIO_STATUS = 0x7a,
  GIVE_SYSTEM_AUDIO_MODE_STATUS = 0x7d,
  SYSTEM_AUDIO_MODE_STATUS = 0x7e,
  ROUTING_CHANGE = 0x80,
  ROUTING_INFORMATION = 0x81,
  ACTIVE_SOURCE = 0x82,
  GIVE_PHYSICAL_ADDRESS = 0x83,
  REPORT_PHYSICAL_ADDRESS = 0x84,
  REQUEST_ACTIVE_SOURCE = 0x85,
  SET_STREAM_PATH = 0x86,
  DEVICE_VENDOR_ID = 0x87,
  VENDOR_COMMAND = 0x89,
  VENDOR_REMOTE_BUTTON_DOWN = 0x8a,
  VENDOR_REMOTE_BUTTON_UP = 0x8b,
  GIVE_DEVICE_VENDOR_ID = 0x8c,
  MENU_REQUEST = 0x8d,
  MENU_STATUS = 0x8e,
  GIVE_DEVICE_POWER_STATUS = 0x8f,
  REPORT_POWER_STATUS = 0x90,
  GET_MENU_LANGUAGE = 0x91,
  SELECT_ANALOGUE_SERVICE = 0x92,
  SELECT_DIGITAL_SERVICE = 0x93,
  SET_DIGITAL_TIMER = 0x97,
  CLEAR_DIGITAL_TIMER = 0x99,
  SET_AUDIO_RATE = 0x9a,
  INACTIVE_SOURCE = 0x9d,
  CEC_VERSION = 0x9e,
  GET_CEC_VERSION = 0x9f,
  VENDOR_COMMAND_WITH_ID = 0xa0,
  CLEAR_EXTERNAL_TIMER = 0xa1,
  SET_EXTERNAL_TIMER = 0xa2,
  REPORT_SHORT_AUDIO_DESCRIPTOR = 0xa3,
  REQUEST_SHORT_AUDIO_DESCRIPTOR = 0xa4,
  GIVE_FEATURES = 0xa5,
  REPORT_FEATURES = 0xa6,
  REQUEST_CURRENT_LATENCY = 0xa7,
  REPORT_CURRENT_LATENCY = 0xa8,
  INITIATE_ARC = 0xc0,
  REPORT_ARC_INITIATED = 0xc1,
  REPORT_ARC_TERMINATED = 0xc2,
  REQUEST_ARC_INITIATION = 0xc3,
  REQUEST_ARC_TERMINATION = 0xc4,
  TERMINATE_ARC = 0xc5,
  CDC_MESSAGE = 0xf8,
  ABORT = 0xff,
}

export enum OperandKind {
  ENUM = 'enum',
  BYTE = 'byte',
  PHYS_ADDR = 'physAddr',
  ASCII = 'ascii',
  VENDOR_ID = 'vendorId',
  HEX = 'hex',
}

export interface EnumOption {
  value: number;
  label: string;
}

export type OperandSpec =
  | { kind: OperandKind.ENUM; name: string; options: EnumOption[] }
  | { kind: OperandKind.BYTE; name: string }
  | { kind: OperandKind.PHYS_ADDR; name: string }
  | { kind: OperandKind.ASCII; name: string; maxLength: number }
  | { kind: OperandKind.VENDOR_ID; name: string }
  | { kind: OperandKind.HEX; name: string };

export enum OpcodeGroup {
  ROUTING = 'Routing',
  POWER = 'Power',
  REMOTE = 'Remote Control',
  INFO = 'Device Info',
  AUDIO = 'Audio',
  ARC = 'ARC',
  DECK = 'Deck / Tuner',
  RECORD = 'Recording / Timers',
  VENDOR = 'Vendor',
  GENERAL = 'General',
}

export interface OpcodeSpec {
  opcode: Opcode;
  name: string;
  group: OpcodeGroup;
  defaultDest: LogicalAddress;
  operands: OperandSpec[];
}

function enumOptions(e: Record<string, string | number>): EnumOption[] {
  return Object.entries(e)
    .filter((entry): entry is [string, number] => typeof entry[1] === 'number')
    .map(([key, value]) => ({ value, label: titleCase(key) }));
}

function titleCase(key: string): string {
  return key
    .toLowerCase()
    .split('_')
    .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
    .join(' ');
}

const options = (pairs: [number, string][]): EnumOption[] => pairs.map(([value, label]) => ({ value, label }));

const physAddr = (name = 'Physical address'): OperandSpec => ({ kind: OperandKind.PHYS_ADDR, name });
const hex = (name = 'Payload (hex)'): OperandSpec => ({ kind: OperandKind.HEX, name });
const byte = (name: string): OperandSpec => ({ kind: OperandKind.BYTE, name });
const choice = (name: string, opts: EnumOption[]): OperandSpec => ({ kind: OperandKind.ENUM, name, options: opts });

const STATUS_REQUEST = options([
  [1, 'On'],
  [2, 'Off'],
  [3, 'Once'],
]);
const ON_OFF = options([
  [1, 'On'],
  [0, 'Off'],
]);
const ABORT_REASON = options([
  [0, 'Unrecognized opcode'],
  [1, 'Not in correct mode'],
  [2, 'Cannot provide source'],
  [3, 'Invalid operand'],
  [4, 'Refused'],
  [5, 'Unable to determine'],
]);
const DEVICE_TYPES = Object.entries(DEVICE_TYPE_NAMES).map(([value, label]) => ({ value: Number(value), label }));

export const UI_COMMAND_OPTIONS = enumOptions(UiCommand);
const POWER_STATUS_OPTIONS = enumOptions(PowerStatus);
const CEC_VERSION_OPTIONS = options([
  [CecVersion.V1_3A, '1.3a'],
  [CecVersion.V1_4, '1.4'],
  [CecVersion.V2_0, '2.0'],
]);

const { TV, AUDIO_SYSTEM, BROADCAST, PLAYBACK_1, RECORDING_1, TUNER_1 } = LogicalAddress;

const spec = (
  opcode: Opcode,
  name: string,
  group: OpcodeGroup,
  defaultDest: LogicalAddress,
  operands: OperandSpec[] = [],
): OpcodeSpec => ({ opcode, name, group, defaultDest, operands });

export const OPCODES: OpcodeSpec[] = [
  spec(Opcode.ACTIVE_SOURCE, 'Active Source', OpcodeGroup.ROUTING, BROADCAST, [physAddr()]),
  spec(Opcode.INACTIVE_SOURCE, 'Inactive Source', OpcodeGroup.ROUTING, TV, [physAddr()]),
  spec(Opcode.REQUEST_ACTIVE_SOURCE, 'Request Active Source', OpcodeGroup.ROUTING, BROADCAST),
  spec(Opcode.SET_STREAM_PATH, 'Set Stream Path', OpcodeGroup.ROUTING, BROADCAST, [physAddr()]),
  spec(Opcode.ROUTING_CHANGE, 'Routing Change', OpcodeGroup.ROUTING, BROADCAST, [
    physAddr('Original address'),
    physAddr('New address'),
  ]),
  spec(Opcode.ROUTING_INFORMATION, 'Routing Information', OpcodeGroup.ROUTING, BROADCAST, [physAddr()]),

  spec(Opcode.IMAGE_VIEW_ON, 'Image View On', OpcodeGroup.POWER, TV),
  spec(Opcode.TEXT_VIEW_ON, 'Text View On', OpcodeGroup.POWER, TV),
  spec(Opcode.STANDBY, 'Standby', OpcodeGroup.POWER, BROADCAST),
  spec(Opcode.GIVE_DEVICE_POWER_STATUS, 'Give Device Power Status', OpcodeGroup.POWER, TV),
  spec(Opcode.REPORT_POWER_STATUS, 'Report Power Status', OpcodeGroup.POWER, TV, [
    choice('Power status', POWER_STATUS_OPTIONS),
  ]),

  spec(Opcode.USER_CONTROL_PRESSED, 'User Control Pressed', OpcodeGroup.REMOTE, TV, [
    choice('Button', UI_COMMAND_OPTIONS),
  ]),
  spec(Opcode.USER_CONTROL_RELEASED, 'User Control Released', OpcodeGroup.REMOTE, TV),
  spec(Opcode.MENU_REQUEST, 'Menu Request', OpcodeGroup.REMOTE, PLAYBACK_1, [
    choice(
      'Request',
      options([
        [0, 'Activate'],
        [1, 'Deactivate'],
        [2, 'Query'],
      ]),
    ),
  ]),
  spec(Opcode.MENU_STATUS, 'Menu Status', OpcodeGroup.REMOTE, TV, [
    choice(
      'Status',
      options([
        [0, 'Activated'],
        [1, 'Deactivated'],
      ]),
    ),
  ]),

  spec(Opcode.GIVE_PHYSICAL_ADDRESS, 'Give Physical Address', OpcodeGroup.INFO, TV),
  spec(Opcode.REPORT_PHYSICAL_ADDRESS, 'Report Physical Address', OpcodeGroup.INFO, BROADCAST, [
    physAddr(),
    choice('Device type', DEVICE_TYPES),
  ]),
  spec(Opcode.GIVE_OSD_NAME, 'Give OSD Name', OpcodeGroup.INFO, TV),
  spec(Opcode.SET_OSD_NAME, 'Set OSD Name', OpcodeGroup.INFO, TV, [
    { kind: OperandKind.ASCII, name: 'Name', maxLength: OSD_NAME_MAX },
  ]),
  spec(Opcode.SET_OSD_STRING, 'Set OSD String', OpcodeGroup.INFO, TV, [
    choice(
      'Display',
      options([
        [0x00, 'Default time'],
        [0x40, 'Until cleared'],
        [0x80, 'Clear previous'],
      ]),
    ),
    { kind: OperandKind.ASCII, name: 'Text', maxLength: OSD_NAME_MAX - 1 },
  ]),
  spec(Opcode.GIVE_DEVICE_VENDOR_ID, 'Give Device Vendor ID', OpcodeGroup.INFO, TV),
  spec(Opcode.DEVICE_VENDOR_ID, 'Device Vendor ID', OpcodeGroup.INFO, BROADCAST, [
    { kind: OperandKind.VENDOR_ID, name: 'Vendor ID' },
  ]),
  spec(Opcode.GET_CEC_VERSION, 'Get CEC Version', OpcodeGroup.INFO, TV),
  spec(Opcode.CEC_VERSION, 'CEC Version', OpcodeGroup.INFO, TV, [choice('Version', CEC_VERSION_OPTIONS)]),
  spec(Opcode.GET_MENU_LANGUAGE, 'Get Menu Language', OpcodeGroup.INFO, TV),
  spec(Opcode.SET_MENU_LANGUAGE, 'Set Menu Language', OpcodeGroup.INFO, BROADCAST, [
    { kind: OperandKind.ASCII, name: 'ISO 639-2 code', maxLength: 3 },
  ]),
  spec(Opcode.GIVE_FEATURES, 'Give Features', OpcodeGroup.INFO, TV),
  spec(Opcode.REPORT_FEATURES, 'Report Features', OpcodeGroup.INFO, BROADCAST, [hex()]),

  spec(Opcode.GIVE_AUDIO_STATUS, 'Give Audio Status', OpcodeGroup.AUDIO, AUDIO_SYSTEM),
  spec(Opcode.REPORT_AUDIO_STATUS, 'Report Audio Status', OpcodeGroup.AUDIO, TV, [byte('Mute bit + volume')]),
  spec(Opcode.SYSTEM_AUDIO_MODE_REQUEST, 'System Audio Mode Request', OpcodeGroup.AUDIO, AUDIO_SYSTEM, [physAddr()]),
  spec(Opcode.SET_SYSTEM_AUDIO_MODE, 'Set System Audio Mode', OpcodeGroup.AUDIO, BROADCAST, [choice('Mode', ON_OFF)]),
  spec(Opcode.GIVE_SYSTEM_AUDIO_MODE_STATUS, 'Give System Audio Mode Status', OpcodeGroup.AUDIO, AUDIO_SYSTEM),
  spec(Opcode.SYSTEM_AUDIO_MODE_STATUS, 'System Audio Mode Status', OpcodeGroup.AUDIO, TV, [choice('Mode', ON_OFF)]),
  spec(Opcode.SET_AUDIO_RATE, 'Set Audio Rate', OpcodeGroup.AUDIO, AUDIO_SYSTEM, [byte('Rate')]),
  spec(Opcode.REQUEST_SHORT_AUDIO_DESCRIPTOR, 'Request Short Audio Descriptor', OpcodeGroup.AUDIO, AUDIO_SYSTEM, [
    hex('Format IDs (hex)'),
  ]),
  spec(Opcode.REPORT_SHORT_AUDIO_DESCRIPTOR, 'Report Short Audio Descriptor', OpcodeGroup.AUDIO, TV, [
    hex('Descriptors (hex)'),
  ]),
  spec(Opcode.REQUEST_CURRENT_LATENCY, 'Request Current Latency', OpcodeGroup.AUDIO, BROADCAST, [physAddr()]),
  spec(Opcode.REPORT_CURRENT_LATENCY, 'Report Current Latency', OpcodeGroup.AUDIO, BROADCAST, [
    physAddr(),
    hex('Latency fields (hex)'),
  ]),

  spec(Opcode.INITIATE_ARC, 'Initiate ARC', OpcodeGroup.ARC, TV),
  spec(Opcode.REPORT_ARC_INITIATED, 'Report ARC Initiated', OpcodeGroup.ARC, AUDIO_SYSTEM),
  spec(Opcode.REPORT_ARC_TERMINATED, 'Report ARC Terminated', OpcodeGroup.ARC, AUDIO_SYSTEM),
  spec(Opcode.REQUEST_ARC_INITIATION, 'Request ARC Initiation', OpcodeGroup.ARC, AUDIO_SYSTEM),
  spec(Opcode.REQUEST_ARC_TERMINATION, 'Request ARC Termination', OpcodeGroup.ARC, AUDIO_SYSTEM),
  spec(Opcode.TERMINATE_ARC, 'Terminate ARC', OpcodeGroup.ARC, TV),

  spec(Opcode.PLAY, 'Play', OpcodeGroup.DECK, PLAYBACK_1, [
    choice(
      'Mode',
      options([
        [0x24, 'Forward'],
        [0x20, 'Reverse'],
        [0x25, 'Still'],
        [0x05, 'Fast forward min'],
        [0x07, 'Fast forward max'],
        [0x09, 'Fast reverse min'],
        [0x0b, 'Fast reverse max'],
      ]),
    ),
  ]),
  spec(Opcode.DECK_CONTROL, 'Deck Control', OpcodeGroup.DECK, PLAYBACK_1, [
    choice(
      'Mode',
      options([
        [1, 'Skip forward'],
        [2, 'Skip reverse'],
        [3, 'Stop'],
        [4, 'Eject'],
      ]),
    ),
  ]),
  spec(Opcode.GIVE_DECK_STATUS, 'Give Deck Status', OpcodeGroup.DECK, PLAYBACK_1, [
    choice('Request', STATUS_REQUEST),
  ]),
  spec(Opcode.DECK_STATUS, 'Deck Status', OpcodeGroup.DECK, TV, [byte('Deck info')]),
  spec(Opcode.GIVE_TUNER_DEVICE_STATUS, 'Give Tuner Device Status', OpcodeGroup.DECK, TUNER_1, [
    choice('Request', STATUS_REQUEST),
  ]),
  spec(Opcode.TUNER_DEVICE_STATUS, 'Tuner Device Status', OpcodeGroup.DECK, TV, [hex('Tuner info (hex)')]),
  spec(Opcode.TUNER_STEP_INCREMENT, 'Tuner Step Increment', OpcodeGroup.DECK, TUNER_1),
  spec(Opcode.TUNER_STEP_DECREMENT, 'Tuner Step Decrement', OpcodeGroup.DECK, TUNER_1),
  spec(Opcode.SELECT_ANALOGUE_SERVICE, 'Select Analogue Service', OpcodeGroup.DECK, TUNER_1, [hex()]),
  spec(Opcode.SELECT_DIGITAL_SERVICE, 'Select Digital Service', OpcodeGroup.DECK, TUNER_1, [hex()]),

  spec(Opcode.RECORD_ON, 'Record On', OpcodeGroup.RECORD, RECORDING_1, [hex('Record source (hex)')]),
  spec(Opcode.RECORD_OFF, 'Record Off', OpcodeGroup.RECORD, RECORDING_1),
  spec(Opcode.RECORD_STATUS, 'Record Status', OpcodeGroup.RECORD, TV, [byte('Status')]),
  spec(Opcode.RECORD_TV_SCREEN, 'Record TV Screen', OpcodeGroup.RECORD, TV),
  spec(Opcode.SET_ANALOGUE_TIMER, 'Set Analogue Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.CLEAR_ANALOGUE_TIMER, 'Clear Analogue Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.SET_DIGITAL_TIMER, 'Set Digital Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.CLEAR_DIGITAL_TIMER, 'Clear Digital Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.SET_EXTERNAL_TIMER, 'Set External Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.CLEAR_EXTERNAL_TIMER, 'Clear External Timer', OpcodeGroup.RECORD, RECORDING_1, [hex()]),
  spec(Opcode.TIMER_STATUS, 'Timer Status', OpcodeGroup.RECORD, TV, [hex()]),
  spec(Opcode.TIMER_CLEARED_STATUS, 'Timer Cleared Status', OpcodeGroup.RECORD, TV, [byte('Status')]),
  spec(Opcode.SET_TIMER_PROGRAM_TITLE, 'Set Timer Program Title', OpcodeGroup.RECORD, RECORDING_1, [
    { kind: OperandKind.ASCII, name: 'Title', maxLength: OSD_NAME_MAX },
  ]),

  spec(Opcode.VENDOR_COMMAND, 'Vendor Command', OpcodeGroup.VENDOR, TV, [hex()]),
  spec(Opcode.VENDOR_COMMAND_WITH_ID, 'Vendor Command With ID', OpcodeGroup.VENDOR, TV, [
    { kind: OperandKind.VENDOR_ID, name: 'Vendor ID' },
    hex(),
  ]),
  spec(Opcode.VENDOR_REMOTE_BUTTON_DOWN, 'Vendor Remote Button Down', OpcodeGroup.VENDOR, TV, [hex()]),
  spec(Opcode.VENDOR_REMOTE_BUTTON_UP, 'Vendor Remote Button Up', OpcodeGroup.VENDOR, TV),
  spec(Opcode.CDC_MESSAGE, 'CDC Message', OpcodeGroup.VENDOR, BROADCAST, [hex()]),

  spec(Opcode.FEATURE_ABORT, 'Feature Abort', OpcodeGroup.GENERAL, TV, [
    byte('Rejected opcode'),
    choice('Reason', ABORT_REASON),
  ]),
  spec(Opcode.ABORT, 'Abort', OpcodeGroup.GENERAL, TV),
];

export const OPCODE_NAMES = new Map(OPCODES.map((s) => [s.opcode, s.name]));

export const OPCODE_GROUPS = Object.values(OpcodeGroup).map((group) => ({
  group,
  specs: OPCODES.filter((s) => s.group === group),
}));
