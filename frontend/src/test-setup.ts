// jsdom has no 2D canvas context, and calling getContext('2d') on it logs a
// noisy "Not implemented" console error for every call. SparklineChart only
// needs the handful of drawing calls below, so a minimal stub lets its
// draw() path run in tests (and be covered) without that console noise.
class FakeCanvasRenderingContext2D {
  fillStyle = '';
  strokeStyle = '';
  lineWidth = 1;
  lineJoin: CanvasLineJoin = 'miter';
  font = '';
  textAlign: CanvasTextAlign = 'left';
  textBaseline: CanvasTextBaseline = 'alphabetic';

  setTransform(): void {}
  clearRect(): void {}
  save(): void {}
  restore(): void {}
  translate(): void {}
  beginPath(): void {}
  moveTo(): void {}
  lineTo(): void {}
  closePath(): void {}
  stroke(): void {}
  fill(): void {}
  fillText(): void {}
  createLinearGradient(): CanvasGradient {
    return { addColorStop: () => undefined } as unknown as CanvasGradient;
  }
  measureText(text: string): TextMetrics {
    // Rough but deterministic width so axisLabelMargin's layout math has
    // something sane to work with in tests -- no real font metrics in jsdom.
    return { width: text.length * 6 } as TextMetrics;
  }
}

HTMLCanvasElement.prototype.getContext = function (
  this: HTMLCanvasElement,
): RenderingContext | null {
  return new FakeCanvasRenderingContext2D() as unknown as RenderingContext;
} as typeof HTMLCanvasElement.prototype.getContext;
