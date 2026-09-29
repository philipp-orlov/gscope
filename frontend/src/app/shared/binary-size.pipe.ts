import { Pipe, PipeTransform } from '@angular/core';

// Formats a value given in MiB as KiB/MiB/GiB, whichever reads best --
// pure and exported so the thresholds are testable without a component.
export function formatBinarySize(valueMib: number): string {
  const safe = Number.isFinite(valueMib) && valueMib > 0 ? valueMib : 0;

  if (safe >= 1024) {
    const gib = safe / 1024;
    return `${gib.toFixed(gib >= 10 ? 1 : 2)} GiB`;
  }
  if (safe >= 1) {
    return `${safe.toFixed(safe >= 10 ? 0 : 1)} MiB`;
  }
  return `${Math.round(safe * 1024)} KiB`;
}

@Pipe({ name: 'binarySize' })
export class BinarySizePipe implements PipeTransform {
  transform(valueMib: number | null | undefined): string {
    return formatBinarySize(valueMib ?? 0);
  }
}
