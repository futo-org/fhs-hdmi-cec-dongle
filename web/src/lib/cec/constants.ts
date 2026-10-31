export enum LogicalAddress {
  TV = 0,
  RECORDING_1 = 1,
  RECORDING_2 = 2,
  TUNER_1 = 3,
  PLAYBACK_1 = 4,
  AUDIO_SYSTEM = 5,
  TUNER_2 = 6,
  TUNER_3 = 7,
  PLAYBACK_2 = 8,
  RECORDING_3 = 9,
  TUNER_4 = 10,
  PLAYBACK_3 = 11,
  BACKUP_1 = 12,
  BACKUP_2 = 13,
  SPECIFIC = 14,
  BROADCAST = 15,
}

export const UNREGISTERED = LogicalAddress.BROADCAST;

export const LOGICAL_ADDRESS_NAMES: Record<LogicalAddress, string> = {
  [LogicalAddress.TV]: 'TV',
  [LogicalAddress.RECORDING_1]: 'Recording 1',
  [LogicalAddress.RECORDING_2]: 'Recording 2',
  [LogicalAddress.TUNER_1]: 'Tuner 1',
  [LogicalAddress.PLAYBACK_1]: 'Playback 1',
  [LogicalAddress.AUDIO_SYSTEM]: 'Audio System',
  [LogicalAddress.TUNER_2]: 'Tuner 2',
  [LogicalAddress.TUNER_3]: 'Tuner 3',
  [LogicalAddress.PLAYBACK_2]: 'Playback 2',
  [LogicalAddress.RECORDING_3]: 'Recording 3',
  [LogicalAddress.TUNER_4]: 'Tuner 4',
  [LogicalAddress.PLAYBACK_3]: 'Playback 3',
  [LogicalAddress.BACKUP_1]: 'Backup 1',
  [LogicalAddress.BACKUP_2]: 'Backup 2',
  [LogicalAddress.SPECIFIC]: 'Specific Use',
  [LogicalAddress.BROADCAST]: 'Broadcast',
};

export const ALL_LOGICAL_ADDRESSES = Object.values(LogicalAddress).filter(
  (v): v is LogicalAddress => typeof v === 'number',
);

export enum DeviceType {
  TV = 0,
  RECORDING = 1,
  RESERVED = 2,
  TUNER = 3,
  PLAYBACK = 4,
  AUDIO_SYSTEM = 5,
  PROCESSOR = 6,
}

export const DEVICE_TYPE_NAMES: Record<DeviceType, string> = {
  [DeviceType.TV]: 'TV',
  [DeviceType.RECORDING]: 'Recording',
  [DeviceType.RESERVED]: 'Reserved',
  [DeviceType.TUNER]: 'Tuner',
  [DeviceType.PLAYBACK]: 'Playback',
  [DeviceType.AUDIO_SYSTEM]: 'Audio System',
  [DeviceType.PROCESSOR]: 'Processor',
};

// Claim order per device type, from the CEC spec.
export const LOGICAL_ADDRESS_CANDIDATES: Partial<Record<DeviceType, LogicalAddress[]>> = {
  [DeviceType.PLAYBACK]: [LogicalAddress.PLAYBACK_1, LogicalAddress.PLAYBACK_2, LogicalAddress.PLAYBACK_3],
  [DeviceType.RECORDING]: [LogicalAddress.RECORDING_1, LogicalAddress.RECORDING_2, LogicalAddress.RECORDING_3],
  [DeviceType.TUNER]: [LogicalAddress.TUNER_1, LogicalAddress.TUNER_2, LogicalAddress.TUNER_3, LogicalAddress.TUNER_4],
  [DeviceType.AUDIO_SYSTEM]: [LogicalAddress.AUDIO_SYSTEM],
  [DeviceType.PROCESSOR]: [LogicalAddress.SPECIFIC],
};

export enum PowerStatus {
  ON = 0,
  STANDBY = 1,
  TO_ON = 2,
  TO_STANDBY = 3,
}

export enum CecVersion {
  V1_3A = 4,
  V1_4 = 5,
  V2_0 = 6,
}

export enum UiCommand {
  SELECT = 0x00,
  UP = 0x01,
  DOWN = 0x02,
  LEFT = 0x03,
  RIGHT = 0x04,
  ROOT_MENU = 0x09,
  SETUP_MENU = 0x0a,
  CONTENTS_MENU = 0x0b,
  BACK = 0x0d,
  NUMBER_0 = 0x20,
  NUMBER_1 = 0x21,
  NUMBER_2 = 0x22,
  NUMBER_3 = 0x23,
  NUMBER_4 = 0x24,
  NUMBER_5 = 0x25,
  NUMBER_6 = 0x26,
  NUMBER_7 = 0x27,
  NUMBER_8 = 0x28,
  NUMBER_9 = 0x29,
  ENTER = 0x2b,
  CHANNEL_UP = 0x30,
  CHANNEL_DOWN = 0x31,
  INPUT_SELECT = 0x34,
  DISPLAY_INFO = 0x35,
  POWER = 0x40,
  VOLUME_UP = 0x41,
  VOLUME_DOWN = 0x42,
  MUTE = 0x43,
  PLAY = 0x44,
  STOP = 0x45,
  PAUSE = 0x46,
  RECORD = 0x47,
  REWIND = 0x48,
  FAST_FORWARD = 0x49,
  EJECT = 0x4a,
  FORWARD = 0x4b,
  BACKWARD = 0x4c,
  EPG = 0x53,
  POWER_TOGGLE = 0x6b,
  POWER_OFF = 0x6c,
  POWER_ON = 0x6d,
  F1_BLUE = 0x71,
  F2_RED = 0x72,
  F3_GREEN = 0x73,
  F4_YELLOW = 0x74,
}

export const DEFAULT_OSD_NAME = 'FHS CEC';
export const OSD_NAME_MAX = 14;
