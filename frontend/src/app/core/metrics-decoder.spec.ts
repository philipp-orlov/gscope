import { MetricsDecoder, NO_TEMPERATURE } from './metrics-decoder';
import { InventoryInit, SampleInit, Update } from './wire/monitor.bufs';

const INVENTORY: InventoryInit = {
  hostname: 'spark',
  kernel: 'Linux 6.8',
  provider: 'nvml',
  cpuCount: 20,
  buildId: 'b1',
  memTotalMib: 124000,
  swapTotalMib: 16000,
  gpus: [{ name: 'NVIDIA GB10', memoryTotalMib: 124000 }],
  sensors: ['CPU', 'SOC'],
};

function wireSample(overrides: SampleInit = {}): SampleInit {
  return {
    timestampMs: 1730000000000n,
    memUsedMib: 5000,
    swapUsedMib: 10,
    rxBytesPerSec: 2097152n,
    txBytesPerSec: 512,
    cores: [{ utilizationCenti: 1234, frequencyMhz: 2400 }],
    gpus: [{ memoryUsedMib: 4096, powerMw: 5600, utilizationCenti: 5000, frequencyMhz: 1800, temperatureDeci: 413 }],
    temperaturesDeci: [552, NO_TEMPERATURE],
    processes: [{ pid: 7, name: 'node', cpuCenti: 15025, memoryKib: 2560 }],
    ...overrides,
  };
}

// A real round trip through the libbufs encoding, as the daemon sends it.
const roundTrip = (inventory: InventoryInit | undefined, samples: SampleInit[]) =>
  Update.decode(Update.encode({ inventory, samples }));

describe('MetricsDecoder', () => {
  it('turns the fixed-point readings back into the dashboard sample', () => {
    const decoded = new MetricsDecoder().apply(roundTrip(INVENTORY, [wireSample()]));
    const [sample] = decoded.samples;
    expect(sample.timestamp).toBe(1730000000000);
    expect(sample.cpu.cores).toEqual([{ utilization: 12.34, frequencyMhz: 2400 }]);
    expect(sample.memory).toEqual({ usedMb: 5000, totalMb: 124000, swapUsedMb: 10, swapTotalMb: 16000 });
    expect(sample.gpus[0]).toEqual({
      name: 'NVIDIA GB10',
      utilization: 50,
      memoryUsedMb: 4096,
      memoryTotalMb: 124000,
      temperatureC: 41.3,
      powerMw: 5600,
      frequencyMhz: 1800,
    });
    expect(sample.processes).toEqual([{ pid: 7, name: 'node', cpu: 150.25, memoryMb: 2.5 }]);
    expect(sample.network).toEqual({ rxBytesPerSec: 2097152, txBytesPerSec: 512 });
  });

  it('names the temperatures from the inventory and leaves out the sensors without a reading', () => {
    const [sample] = new MetricsDecoder().apply(roundTrip(INVENTORY, [wireSample()])).samples;
    expect(sample.temperatures).toEqual([{ name: 'CPU', celsius: 55.2 }]);
  });

  it('reports the system and build from an inventory, and keeps the inventory for the updates without one', () => {
    const decoder = new MetricsDecoder();
    const first = decoder.apply(roundTrip(INVENTORY, []));
    expect(first.system).toEqual({ hostname: 'spark', kernel: 'Linux 6.8', provider: 'nvml', cpuCount: 20 });
    expect(first.buildId).toBe('b1');

    const later = decoder.apply(roundTrip(undefined, [wireSample()]));
    expect(later.system).toBeNull();
    expect(later.buildId).toBeNull();
    expect(later.samples[0].gpus[0].name).toBe('NVIDIA GB10');
    expect(later.samples[0].memory.totalMb).toBe(124000);
  });

  it('maps a GPU without a temperature to the sentinel the dashboard tests for', () => {
    const gpus = [{ memoryUsedMib: 1, powerMw: -1, utilizationCenti: 0, frequencyMhz: 0, temperatureDeci: NO_TEMPERATURE }];
    const [sample] = new MetricsDecoder().apply(roundTrip(INVENTORY, [wireSample({ gpus })])).samples;
    expect(sample.gpus[0].temperatureC).toBe(-1000);
    expect(sample.gpus[0].powerMw).toBe(-1);
  });

  it('copes with samples before any inventory', () => {
    const [sample] = new MetricsDecoder().apply(roundTrip(undefined, [wireSample()])).samples;
    expect(sample.gpus[0].name).toBe('GPU 0');
    expect(sample.temperatures).toEqual([]);
  });

  it('sends far less than the JSON it replaces', () => {
    const live = Update.encode({ samples: [wireSample()] });
    expect(live.byteLength).toBeLessThan(400);
  });
});
