// Wire schema shared with the backend (see backend/README.md). Kept as
// plain interfaces -- these are DTOs, not behavior.

export interface CpuCoreSample {
  utilization: number; // 0..100
  frequencyMhz: number;
}

export interface GpuSample {
  name: string;
  utilization: number; // 0..100
  memoryUsedMb: number;
  memoryTotalMb: number;
  temperatureC: number; // sentinel: -1000 when unavailable
  powerMw: number; // sentinel: -1 when unavailable
  frequencyMhz: number;
}

export interface TemperatureSample {
  name: string;
  celsius: number;
}

export interface ProcessSample {
  pid: number;
  name: string;
  cpu: number; // %, relative to one core -- a multi-threaded process can exceed 100
  memoryMb: number;
}

export interface MemorySample {
  usedMb: number;
  totalMb: number;
  swapUsedMb: number;
  swapTotalMb: number;
}

export interface NetworkSample {
  rxBytesPerSec: number;
  txBytesPerSec: number;
}

export interface Sample {
  timestamp: number; // ms since epoch
  cpu: { cores: CpuCoreSample[] };
  memory: MemorySample;
  gpus: GpuSample[];
  processes: ProcessSample[];
  network: NetworkSample;
  temperatures: TemperatureSample[];
}

export interface SystemInfo {
  hostname: string;
  kernel: string;
  provider: string;
  cpuCount: number;
}

export type WireMessage =
  | { type: 'history'; samples: Sample[]; buildId?: string }
  | { type: 'sample'; sample: Sample; buildId?: string };

export type ConnectionStatus = 'connecting' | 'open' | 'closed';
