import { formatBinarySize } from './binary-size.pipe';

describe('formatBinarySize', () => {
  it('formats sub-MiB values as KiB', () => {
    expect(formatBinarySize(0.5)).toBe('512 KiB');
    expect(formatBinarySize(0)).toBe('0 KiB');
  });

  it('formats values under 1024 MiB as MiB', () => {
    expect(formatBinarySize(1)).toBe('1.0 MiB');
    expect(formatBinarySize(23)).toBe('23 MiB');
    expect(formatBinarySize(1023)).toBe('1023 MiB');
  });

  it('formats values at or above 1024 MiB as GiB', () => {
    expect(formatBinarySize(1024)).toBe('1.00 GiB');
    expect(formatBinarySize(1536)).toBe('1.50 GiB');
    expect(formatBinarySize(15524)).toBe('15.2 GiB');
  });

  it('treats negative/NaN/undefined as 0', () => {
    expect(formatBinarySize(-5)).toBe('0 KiB');
    expect(formatBinarySize(NaN)).toBe('0 KiB');
  });
});
