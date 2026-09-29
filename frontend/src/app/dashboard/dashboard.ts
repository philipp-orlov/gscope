import { ChangeDetectionStrategy, Component, HostListener, computed, inject, signal } from '@angular/core';
import { DecimalPipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatCardModule } from '@angular/material/card';
import { MatIconModule } from '@angular/material/icon';
import { MatMenuModule } from '@angular/material/menu';
import { MatSortModule, Sort } from '@angular/material/sort';
import { MatTableModule } from '@angular/material/table';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';

import { MetricsService } from '../core/metrics.service';
import { ProcessSample, Sample, TemperatureSample } from '../models/metrics.model';
import { BinarySizePipe } from '../shared/binary-size.pipe';
import { SparklineChart, SparklineSeries } from '../shared/sparkline-chart/sparkline-chart';

// Pure data-shaping helpers, exported so they're testable without
// rendering the component or touching a canvas.

export function average(values: number[]): number {
  if (values.length === 0) return 0;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

export function cpuUtilizationSeries(history: Sample[]): number[] {
  return history.map((sample) => average(sample.cpu.cores.map((core) => core.utilization)));
}

export function temperatureSeries(history: Sample[], sensorName: string): number[] {
  return history.map((sample) => sample.temperatures.find((t) => t.name === sensorName)?.celsius ?? 0);
}

export function memoryPercentSeries(history: Sample[]): number[] {
  return history.map((sample) =>
    sample.memory.totalMb > 0 ? (sample.memory.usedMb / sample.memory.totalMb) * 100 : 0,
  );
}

export function swapPercentSeries(history: Sample[]): number[] {
  return history.map((sample) =>
    sample.memory.swapTotalMb > 0 ? (sample.memory.swapUsedMb / sample.memory.swapTotalMb) * 100 : 0,
  );
}

export function gpuCount(history: Sample[]): number {
  return history.length > 0 ? history[history.length - 1].gpus.length : 0;
}

// Drivers report GPU names prefixed with the vendor ("NVIDIA GeForce RTX
// 4090", "NVIDIA GB10", ...); redundant on a card whose header already
// says "NVIDIA TITAN RTX" repeated across every tile, so it's trimmed for
// display only -- the raw name (used elsewhere, e.g. tests) is untouched.
export function gpuDisplayName(name: string): string {
  return name.replace(/^NVIDIA\s+/i, '');
}

// Core clock tooltip text -- GHz reads better than a 4-digit MHz number
// for modern CPUs; one decimal place is as precise as a glance needs.
export function coreTooltip(core: { utilization: number; frequencyMhz: number }): string {
  return `${core.utilization}% @ ${(core.frequencyMhz / 1000).toFixed(1)} GHz`;
}

export function gpuUtilizationSeries(history: Sample[], index: number): number[] {
  return history.map((sample) => sample.gpus[index]?.utilization ?? 0);
}

// GpuSample.temperatureC uses -1000 as an "unavailable" sentinel; charted
// as 0 rather than distorting the whole series' scale.
export function gpuTemperatureSeries(history: Sample[], index: number): number[] {
  return history.map((sample) => {
    const value = sample.gpus[index]?.temperatureC;
    return value === undefined || value <= -900 ? 0 : value;
  });
}

export function gpuMemoryPercentSeries(history: Sample[], index: number): number[] {
  return history.map((sample) => {
    const gpu = sample.gpus[index];
    return gpu && gpu.memoryTotalMb > 0 ? (gpu.memoryUsedMb / gpu.memoryTotalMb) * 100 : 0;
  });
}

const BYTES_PER_MIB = 1024 * 1024;

export function networkRxSeries(history: Sample[]): number[] {
  return history.map((sample) => sample.network.rxBytesPerSec / BYTES_PER_MIB);
}

export function networkTxSeries(history: Sample[]): number[] {
  return history.map((sample) => sample.network.txBytesPerSec / BYTES_PER_MIB);
}

// The chart's [max] input has to be a concrete number (percent charts
// hardcode 100); network throughput has no natural ceiling, so this picks
// one from the data itself -- some headroom above the highest point seen,
// with a 1 MiB/s floor so an idle link doesn't render as a flat line
// pinned to the top of the chart.
export function networkChartMax(rxSeries: number[], txSeries: number[]): number {
  const peak = Math.max(1, ...rxSeries, ...txSeries);
  return Math.ceil(peak * 1.2 * 10) / 10;
}

// The single "how busy is this right now" number shown to the right of
// each tile's header.
export function cpuUtilizationNow(sample: Sample): number {
  return average(sample.cpu.cores.map((core) => core.utilization));
}

export function memoryPercentNow(sample: Sample): number {
  return sample.memory.totalMb > 0 ? (sample.memory.usedMb / sample.memory.totalMb) * 100 : 0;
}

export function networkTotalBytesPerSec(sample: Sample): number {
  return sample.network.rxBytesPerSec + sample.network.txBytesPerSec;
}

export function sortProcesses(processes: ProcessSample[], sort: Sort): ProcessSample[] {
  if (!sort.direction) return processes;
  const factor = sort.direction === 'asc' ? 1 : -1;
  return [...processes].sort((a, b) => {
    switch (sort.active) {
      case 'pid':
        return factor * (a.pid - b.pid);
      case 'name':
        return factor * a.name.localeCompare(b.name);
      case 'memoryMb':
        return factor * (a.memoryMb - b.memoryMb);
      default:
        return factor * (a.cpu - b.cpu);
    }
  });
}

export function sortTemperatures(temperatures: TemperatureSample[], sort: Sort): TemperatureSample[] {
  if (!sort.direction) return temperatures;
  const factor = sort.direction === 'asc' ? 1 : -1;
  return [...temperatures].sort((a, b) => {
    switch (sort.active) {
      case 'name':
        return factor * a.name.localeCompare(b.name);
      default:
        return factor * (a.celsius - b.celsius);
    }
  });
}

const STATUS_LABELS: Record<string, string> = {
  connecting: 'Connecting…',
  open: 'Live',
  closed: 'Disconnected',
};

export type TileKey = 'cpu' | 'gpu' | 'memory' | 'network' | 'temperatures' | 'processes';

// One letter per tile -- keydown handler below looks this up by
// event.key so the shortcut always matches what's shown in each tile's
// tooltip, no matter how the tiles get reordered in the template.
const TILE_SHORTCUTS: Record<string, TileKey> = {
  c: 'cpu',
  g: 'gpu',
  m: 'memory',
  n: 'network',
  t: 'temperatures',
  p: 'processes',
};

// Label + shortcut letter shown per row in the tile-visibility menu, in
// display order -- built from TILE_SHORTCUTS so the two never drift apart.
export const TILE_MENU_ITEMS: { tile: TileKey; label: string; shortcut: string }[] = [
  { tile: 'cpu', label: 'CPU', shortcut: 'C' },
  { tile: 'gpu', label: 'GPU', shortcut: 'G' },
  { tile: 'memory', label: 'Memory', shortcut: 'M' },
  { tile: 'network', label: 'Network', shortcut: 'N' },
  { tile: 'temperatures', label: 'Temperatures', shortcut: 'T' },
  { tile: 'processes', label: 'Processes', shortcut: 'P' },
];

@Component({
  selector: 'gscope-dashboard',
  imports: [
    BinarySizePipe,
    DecimalPipe,
    MatButtonModule,
    MatCardModule,
    MatIconModule,
    MatMenuModule,
    MatSortModule,
    MatTableModule,
    MatToolbarModule,
    MatTooltipModule,
    SparklineChart,
  ],
  templateUrl: './dashboard.html',
  styleUrl: './dashboard.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Dashboard {
  private readonly metrics = inject(MetricsService);

  // Bound so the template can call them directly on a `sample` it already
  // has in scope, e.g. {{ cpuUtilizationNow(sample) }} -- see
  // cpuUtilizationNow()/etc. above for the actual math.
  readonly cpuUtilizationNow = cpuUtilizationNow;
  readonly memoryPercentNow = memoryPercentNow;
  readonly networkTotalBytesPerSec = networkTotalBytesPerSec;
  readonly gpuDisplayName = gpuDisplayName;
  readonly coreTooltip = coreTooltip;

  // Split for the letter-by-letter glow sweep in the toolbar (see
  // dashboard.html/scss) -- a plain CSS animation, so it repeats on its
  // own with no timer/interval to manage here. Each letter renders in its
  // own inline-block span, and a lone regular space is the sole content
  // of its span -- CSS collapses that away as leading/trailing
  // whitespace, so a non-breaking space is used instead to keep the gap.
  readonly titleLetters = 'GPU\u00A0Scope'.split('');

  readonly status = this.metrics.status;
  readonly latest = this.metrics.latest;
  readonly systemInfo = this.metrics.systemInfo;
  readonly statusLabel = computed(() => STATUS_LABELS[this.status()] ?? this.status());
  readonly processColumns = ['pid', 'name', 'cpu', 'memoryMb'];
  readonly temperatureColumns = ['name', 'celsius'];

  readonly processSort = signal<Sort>({ active: 'cpu', direction: 'desc' });
  readonly temperatureSort = signal<Sort>({ active: 'celsius', direction: 'desc' });

  readonly sortedProcesses = computed(() => sortProcesses(this.latest()?.processes ?? [], this.processSort()));
  readonly sortedTemperatures = computed(() =>
    sortTemperatures(this.latest()?.temperatures ?? [], this.temperatureSort()),
  );

  // Temperatures starts hidden -- it needs thermal_zone sysfs data most
  // machines don't expose (see backend README), so an empty table isn't a
  // useful default view.
  private readonly hiddenTiles = signal<ReadonlySet<TileKey>>(new Set<TileKey>(['temperatures']));

  readonly tileMenuItems = TILE_MENU_ITEMS;

  isTileVisible(tile: TileKey): boolean {
    return !this.hiddenTiles().has(tile);
  }

  toggleTile(tile: TileKey): void {
    this.hiddenTiles.update((hidden) => {
      const next = new Set(hidden);
      if (next.has(tile)) next.delete(tile);
      else next.add(tile);
      return next;
    });
  }

  // c/g/m/n/t/p toggle CPU/GPU/Memory/Network/Temperatures/Processes;
  // ignored while typing anywhere focusable, or with a modifier held, so
  // it doesn't fight the browser's own shortcuts or (future) text inputs.
  @HostListener('window:keydown', ['$event'])
  onWindowKeydown(event: KeyboardEvent): void {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const target = event.target as HTMLElement | null;
    if (target && /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName)) return;

    const tile = TILE_SHORTCUTS[event.key.toLowerCase()];
    if (!tile) return;

    event.preventDefault();
    this.toggleTile(tile);
  }

  readonly cpuChart = computed<SparklineSeries[]>(() => [
    { label: 'Utilization', color: '#4caf50', values: cpuUtilizationSeries(this.metrics.history()), fill: true, unit: '%' },
    {
      label: 'Temperature',
      color: '#ff9800',
      values: temperatureSeries(this.metrics.history(), 'CPU'),
      unit: '°C',
    },
  ]);

  readonly memoryChart = computed<SparklineSeries[]>(() => [
    { label: 'Memory', color: '#ec407a', values: memoryPercentSeries(this.metrics.history()), fill: true, unit: '%' },
    { label: 'Swap', color: '#42a5f5', values: swapPercentSeries(this.metrics.history()), unit: '%' },
  ]);

  readonly networkChart = computed<SparklineSeries[]>(() => {
    const history = this.metrics.history();
    const rx = networkRxSeries(history);
    const tx = networkTxSeries(history);
    return [
      { label: 'In', color: '#26c6da', values: rx, fill: true, unit: ' MiB/s' },
      { label: 'Out', color: '#ffb300', values: tx, unit: ' MiB/s' },
    ];
  });
  readonly networkMax = computed(() => {
    const history = this.metrics.history();
    return networkChartMax(networkRxSeries(history), networkTxSeries(history));
  });

  readonly gpuIndices = computed<number[]>(() => {
    const count = gpuCount(this.metrics.history());
    return Array.from({ length: count }, (_, index) => index);
  });

  gpuChart(index: number): SparklineSeries[] {
    const history = this.metrics.history();
    return [
      {
        label: 'Utilization',
        color: '#7e57c2',
        values: gpuUtilizationSeries(history, index),
        fill: true,
        unit: '%',
      },
      {
        label: 'Memory',
        color: '#26c6da',
        values: gpuMemoryPercentSeries(history, index),
        unit: '%',
      },
    ];
  }
}
