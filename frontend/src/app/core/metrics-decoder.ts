// Turns the wire updates of Monitor.Watch (backend/include/gscope/wire/monitor.bufs) back into the dashboard's
// Sample objects. The wire leaves out what does not change: the Inventory (GPU names, sensor names, memory totals,
// the host) comes in the first update and again only when it changes, and readings are fixed point. This keeps the
// last inventory and puts the pieces together. Free of Angular imports on purpose.

import { Inventory, Sample as WireSample, Update } from './wire/monitor.bufs';
import { Sample, SystemInfo, TemperatureSample } from '../models/metrics.model';

/** Gpu.temperature_deci / Sample.temperatures_deci when there is no reading (INT16_MIN). */
export const NO_TEMPERATURE = -32768;

/** What the dashboard reads for "no GPU temperature" (the providers' sentinel). */
const GPU_NO_TEMPERATURE = -1000;

export interface DecodedUpdate {
  samples: Sample[];
  /** Set when the update brought an inventory. */
  system: SystemInfo | null;
  /** Set when the update brought an inventory: the served frontend's fingerprint. */
  buildId: string | null;
}

export class MetricsDecoder {
  private inventory: Inventory | null = null;

  /** Forgets the inventory: a new connection starts with one. */
  reset(): void {
    this.inventory = null;
  }

  apply(update: Update): DecodedUpdate {
    const inventory = update.inventory;
    if (inventory) this.inventory = inventory;
    return {
      samples: update.samples.map((s) => this.sample(s)),
      system: inventory
        ? { hostname: inventory.hostname, kernel: inventory.kernel, provider: inventory.provider, cpuCount: inventory.cpuCount }
        : null,
      buildId: inventory ? inventory.buildId : null,
    };
  }

  private sample(s: WireSample): Sample {
    const inv = this.inventory;
    const gpuInfo = inv?.gpus ?? [];
    const sensors = inv?.sensors ?? [];
    const temperatures: TemperatureSample[] = [];
    s.temperaturesDeci.forEach((deci, i) => {
      if (deci !== NO_TEMPERATURE && i < sensors.length) temperatures.push({ name: sensors[i], celsius: deci / 10 });
    });
    return {
      timestamp: Number(s.timestampMs),
      cpu: { cores: s.cores.map((c) => ({ utilization: c.utilizationCenti / 100, frequencyMhz: c.frequencyMhz })) },
      memory: {
        usedMb: s.memUsedMib,
        totalMb: inv?.memTotalMib ?? 0,
        swapUsedMb: s.swapUsedMib,
        swapTotalMb: inv?.swapTotalMib ?? 0,
      },
      gpus: s.gpus.map((g, i) => ({
        name: gpuInfo[i]?.name ?? `GPU ${i}`,
        utilization: g.utilizationCenti / 100,
        memoryUsedMb: g.memoryUsedMib,
        memoryTotalMb: gpuInfo[i]?.memoryTotalMib ?? 0,
        temperatureC: g.temperatureDeci === NO_TEMPERATURE ? GPU_NO_TEMPERATURE : g.temperatureDeci / 10,
        powerMw: g.powerMw,
        frequencyMhz: g.frequencyMhz,
      })),
      processes: s.processes.map((p) => ({ pid: p.pid, name: p.name, cpu: p.cpuCenti / 100, memoryMb: p.memoryKib / 1024 })),
      network: { rxBytesPerSec: Number(s.rxBytesPerSec), txBytesPerSec: Number(s.txBytesPerSec) },
      temperatures,
    };
  }
}
