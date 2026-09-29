import { TestBed } from '@angular/core/testing';

import { SparklineChart, computePoints, indexFromX } from './sparkline-chart';

describe('computePoints', () => {
  it('returns an empty array for no values', () => {
    expect(computePoints([], 100, 50, 100)).toEqual([]);
  });

  it('maps a single value to the right edge', () => {
    const [point] = computePoints([50], 100, 50, 100);
    expect(point.x).toBe(100);
    expect(point.y).toBe(25); // 50% of max -> mid-height
  });

  it('spreads values evenly across the width, oldest first', () => {
    const points = computePoints([0, 50, 100], 100, 50, 100);
    expect(points.map((p) => p.x)).toEqual([0, 50, 100]);
    expect(points[0].y).toBe(50); // 0% utilization -> bottom
    expect(points[2].y).toBe(0); // 100% utilization -> top
  });

  it('clamps out-of-range and NaN values instead of drawing off-chart', () => {
    const points = computePoints([-10, 200, NaN], 100, 50, 100);
    expect(points[0].y).toBe(50); // clamped to 0
    expect(points[1].y).toBe(0); // clamped to max
    expect(points[2].y).toBe(50); // NaN treated as 0
  });
});

describe('indexFromX', () => {
  it('maps the left/right edges to the first/last index', () => {
    expect(indexFromX(0, 100, 5)).toBe(0);
    expect(indexFromX(100, 100, 5)).toBe(4);
  });

  it('rounds to the nearest index', () => {
    expect(indexFromX(24, 100, 5)).toBe(1); // 25 is index 1
    expect(indexFromX(1, 100, 5)).toBe(0);
  });

  it('clamps positions outside the plot area', () => {
    expect(indexFromX(-50, 100, 5)).toBe(0);
    expect(indexFromX(500, 100, 5)).toBe(4);
  });

  it('always returns 0 for a single point', () => {
    expect(indexFromX(50, 100, 1)).toBe(0);
    expect(indexFromX(0, 100, 0)).toBe(0);
  });
});

describe('SparklineChart', () => {
  it('creates and draws without throwing (canvas 2D context stubbed in test-setup.ts)', () => {
    TestBed.configureTestingModule({ imports: [SparklineChart] });
    const fixture = TestBed.createComponent(SparklineChart);
    fixture.componentRef.setInput('series', [{ label: 'CPU', color: '#4caf50', values: [1, 2, 3] }]);
    expect(() => fixture.detectChanges()).not.toThrow();
  });

  it('handles a mousemove/mouseleave cycle without throwing', () => {
    TestBed.configureTestingModule({ imports: [SparklineChart] });
    const fixture = TestBed.createComponent(SparklineChart);
    fixture.componentRef.setInput('series', [{ label: 'CPU', color: '#4caf50', values: [1, 2, 3] }]);
    fixture.detectChanges();

    const canvas: HTMLCanvasElement = fixture.nativeElement.querySelector('canvas');
    const moveEvent = new MouseEvent('mousemove');
    Object.defineProperty(moveEvent, 'offsetX', { value: 40 });
    expect(() => canvas.dispatchEvent(moveEvent)).not.toThrow();
    expect(() => canvas.dispatchEvent(new MouseEvent('mouseleave'))).not.toThrow();
  });
});
