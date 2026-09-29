import {
  AfterViewInit,
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  effect,
  input,
  viewChild,
} from '@angular/core';

export interface SparklineSeries {
  label: string;
  color: string;
  values: number[]; // oldest first, same length across series in one chart
  fill?: boolean;
  unit?: string; // overrides leftUnit in the hover readout only (e.g. temperature vs %)
}

interface Point {
  x: number;
  y: number;
}

interface ChartLayout {
  cssWidth: number;
  cssHeight: number;
  leftMargin: number;
  rightMargin: number;
  plotWidth: number;
}

// Maps values (0..max) onto pixel coordinates across the given width/height,
// oldest-first left-to-right. Pure and exported so the geometry can be
// tested without a canvas (jsdom has no 2D context).
export function computePoints(values: number[], width: number, height: number, max: number): Point[] {
  if (values.length === 0) return [];
  if (values.length === 1) {
    return [{ x: width, y: height - (clamp(values[0], max) / max) * height }];
  }
  const stepX = width / (values.length - 1);
  return values.map((value, index) => ({
    x: index * stepX,
    y: height - (clamp(value, max) / max) * height,
  }));
}

// Maps a mouse x position (relative to the plot area, i.e. already offset
// by the left margin) to the nearest data index. Pure and exported so the
// hover math is testable without a canvas or a real mouse event.
export function indexFromX(localX: number, plotWidth: number, count: number): number {
  if (count <= 1) return 0;
  const stepX = plotWidth / (count - 1);
  const index = Math.round(localX / stepX);
  return Math.min(count - 1, Math.max(0, index));
}

function clamp(value: number, max: number): number {
  if (Number.isNaN(value)) return 0;
  return Math.min(Math.max(value, 0), max);
}

// Widest of the three tick labels drawn for one side (top/mid/bottom),
// plus padding -- never narrower than minMargin. Returns minMargin
// unchanged if there's no canvas context yet (e.g. before first paint).
function axisLabelMargin(
  context: CanvasRenderingContext2D | null,
  max: number,
  unit: string,
  minMargin: number,
): number {
  if (!context) return minMargin;
  context.font = '10px system-ui, sans-serif';
  const widest = Math.max(
    context.measureText(`${max}${unit}`).width,
    context.measureText(`${max / 2}${unit}`).width,
    context.measureText(`0${unit}`).width,
  );
  return Math.max(minMargin, Math.ceil(widest) + 8);
}

// A compact, dependency-free scrolling multi-line chart -- the "compressed
// utilization graph" look: a handful of overlaid series (e.g. utilization %
// and temperature) redrawn only when new data arrives, never on a timer, so
// an idle dashboard costs nothing.
@Component({
  selector: 'gscope-sparkline-chart',
  templateUrl: './sparkline-chart.html',
  styleUrl: './sparkline-chart.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class SparklineChart implements AfterViewInit {
  readonly series = input<SparklineSeries[]>([]);
  readonly max = input<number>(100);
  readonly height = input<number>(96);
  // Unit suffix for the left-hand scale (the axis every series is actually
  // plotted against). rightUnit adds a second scale on the right showing
  // the same tick values in a different unit -- e.g. a %/°C chart where a
  // temperature series is plotted on the same 0..max axis as utilization.
  readonly leftUnit = input<string>('%');
  readonly rightUnit = input<string | undefined>(undefined);

  private readonly canvasRef = viewChild.required<ElementRef<HTMLCanvasElement>>('canvas');
  private ready = false;
  private hoverIndex: number | null = null;
  private rafScheduled = false;

  constructor() {
    // Redraw whenever series/max/height change, and nothing else -- no
    // requestAnimationFrame loop running while the tab just sits there
    // between WebSocket pushes.
    effect(() => {
      this.series();
      this.max();
      this.height();
      this.draw();
    });
  }

  ngAfterViewInit(): void {
    this.ready = true;
    this.draw();
  }

  onMouseMove(event: MouseEvent): void {
    const layout = this.computeLayout();
    const count = this.series()[0]?.values.length ?? 0;
    if (count === 0) return;
    this.hoverIndex = indexFromX(event.offsetX - layout.leftMargin, layout.plotWidth, count);
    this.scheduleDraw();
  }

  onMouseLeave(): void {
    this.hoverIndex = null;
    this.scheduleDraw();
  }

  // Mouse move fires far more often than data arrives; batching redraws
  // onto the next frame avoids drawing the same hover state more than
  // once per frame while the pointer is in motion.
  private scheduleDraw(): void {
    if (this.rafScheduled) return;
    this.rafScheduled = true;
    requestAnimationFrame(() => {
      this.rafScheduled = false;
      this.draw();
    });
  }

  private computeLayout(): ChartLayout {
    const canvas = this.canvasRef().nativeElement;
    const cssWidth = canvas.clientWidth || 300;
    const cssHeight = this.height();
    const max = this.max();
    const leftUnit = this.leftUnit();
    const rightUnit = this.rightUnit();
    const context = canvas.getContext('2d');
    // Margins sized from the actual label text (e.g. " MiB/s" is far wider
    // than "%"/"°C") instead of a fixed width -- a fixed 30px margin fit
    // the short units fine but clipped longer ones like the Network
    // chart's " MiB/s" right off the left edge of the canvas.
    const leftMargin = axisLabelMargin(context, max, leftUnit, 30);
    const rightMargin = rightUnit ? axisLabelMargin(context, max, rightUnit, 34) : 4;
    const plotWidth = Math.max(0, cssWidth - leftMargin - rightMargin);
    return { cssWidth, cssHeight, leftMargin, rightMargin, plotWidth };
  }

  draw(): void {
    if (!this.ready) return;
    const canvas = this.canvasRef().nativeElement;
    const context = canvas.getContext('2d');
    if (!context) return; // headless test environment without a 2D context

    const { cssWidth, cssHeight, leftMargin, rightMargin, plotWidth } = this.computeLayout();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = cssWidth * dpr;
    canvas.height = cssHeight * dpr;
    context.setTransform(dpr, 0, 0, dpr, 0, 0);
    context.clearRect(0, 0, cssWidth, cssHeight);

    const max = this.max();
    const rightUnit = this.rightUnit();
    const series = this.series();

    context.save();
    context.translate(leftMargin, 0);
    drawGrid(context, plotWidth, cssHeight);

    for (const s of series) {
      const points = computePoints(s.values, plotWidth, cssHeight, max);
      if (points.length < 2) continue;
      drawSeries(context, points, s, cssHeight);
    }

    if (this.hoverIndex !== null) {
      drawHover(context, series, this.hoverIndex, plotWidth, cssHeight, max, this.leftUnit());
    }
    context.restore();

    drawAxisLabels(context, leftMargin, cssWidth - rightMargin, cssHeight, max, this.leftUnit(), rightUnit);
  }
}

// Angular's signal inputs trigger change detection but not an explicit
// "on change" hook by default outside effect(); this keeps the redraw wiring
// in one small helper so the constructor above stays readable.
function drawGrid(context: CanvasRenderingContext2D, width: number, height: number): void {
  context.strokeStyle = 'rgba(255, 255, 255, 0.12)';
  context.lineWidth = 1;
  for (const fraction of [0, 0.25, 0.5, 0.75, 1]) {
    const y = Math.round(height * fraction) + 0.5;
    context.beginPath();
    context.moveTo(0, y);
    context.lineTo(width, y);
    context.stroke();
  }
  // Vertical lines are a touch fainter than the horizontal ones -- a
  // reference grid, not a second series -- matching the reference
  // dashboard's barely-there crosshatch.
  context.strokeStyle = 'rgba(255, 255, 255, 0.07)';
  for (const fraction of [0, 0.2, 0.4, 0.6, 0.8, 1]) {
    const x = Math.round(width * fraction) + 0.5;
    context.beginPath();
    context.moveTo(x, 0);
    context.lineTo(x, height);
    context.stroke();
  }
}

// Tick text at the top/middle/bottom of the plot area -- left side in
// `leftUnit` (the axis every series is actually plotted against), right
// side in `rightUnit` when given (same tick positions, a second unit for a
// series like temperature that shares the same 0..max axis).
function drawAxisLabels(
  context: CanvasRenderingContext2D,
  leftEdge: number,
  rightEdge: number,
  height: number,
  max: number,
  leftUnit: string,
  rightUnit: string | undefined,
): void {
  context.font = '10px system-ui, sans-serif';
  context.fillStyle = 'rgba(255, 255, 255, 0.55)';

  context.textAlign = 'right';
  context.textBaseline = 'top';
  context.fillText(`${max}${leftUnit}`, leftEdge - 4, 2);
  context.textBaseline = 'middle';
  context.fillText(`${max / 2}${leftUnit}`, leftEdge - 4, height / 2);
  context.textBaseline = 'bottom';
  context.fillText(`0${leftUnit}`, leftEdge - 4, height - 2);

  if (!rightUnit) return;
  context.textAlign = 'left';
  context.textBaseline = 'top';
  context.fillText(`${max}${rightUnit}`, rightEdge + 4, 2);
  context.textBaseline = 'middle';
  context.fillText(`${max / 2}${rightUnit}`, rightEdge + 4, height / 2);
  context.textBaseline = 'bottom';
  context.fillText(`0${rightUnit}`, rightEdge + 4, height - 2);
}

// A heavier vertical line at the hovered index plus a dot and a value
// readout per series -- all drawn within the same translated (plot-area)
// coordinate space as the series themselves.
function drawHover(
  context: CanvasRenderingContext2D,
  series: SparklineSeries[],
  hoverIndex: number,
  plotWidth: number,
  height: number,
  max: number,
  leftUnit: string,
): void {
  const count = series[0]?.values.length ?? 0;
  if (count === 0) return;
  // Mirrors computePoints()'s own x mapping (including its single-point ->
  // right-edge special case) so the hover line lines up with the dots.
  const x = count === 1 ? plotWidth : (hoverIndex * plotWidth) / (count - 1);

  context.strokeStyle = 'rgba(255, 255, 255, 0.45)';
  context.lineWidth = 1.5;
  context.beginPath();
  context.moveTo(x, 0);
  context.lineTo(x, height);
  context.stroke();

  context.font = '10px system-ui, sans-serif';
  context.textAlign = 'left';
  context.textBaseline = 'top';
  let labelY = 2;
  for (const s of series) {
    const value = s.values[hoverIndex];
    if (value === undefined) continue;

    const point = computePoints(s.values, plotWidth, height, max)[hoverIndex];
    if (point) {
      context.beginPath();
      context.arc(point.x, point.y, 2.5, 0, Math.PI * 2);
      context.fillStyle = s.color;
      context.fill();
    }

    const labelX = Math.min(x + 6, Math.max(0, plotWidth - 90));
    context.fillStyle = s.color;
    const unit = s.unit ?? leftUnit;
    const decimals = unit === '%' ? 2 : 1;  // percent readouts get 2 decimal places, like everywhere else
    context.fillText(`${s.label}: ${Number.isFinite(value) ? value.toFixed(decimals) : '–'}${unit}`, labelX, labelY);
    labelY += 12;
  }
}

function drawSeries(
  context: CanvasRenderingContext2D,
  points: Point[],
  series: SparklineSeries,
  height: number,
): void {
  if (series.fill) {
    context.beginPath();
    context.moveTo(points[0].x, height);
    for (const point of points) context.lineTo(point.x, point.y);
    context.lineTo(points[points.length - 1].x, height);
    context.closePath();
    const gradient = context.createLinearGradient(0, 0, 0, height);
    gradient.addColorStop(0, toAlpha(series.color, 0.35));
    gradient.addColorStop(1, toAlpha(series.color, 0.02));
    context.fillStyle = gradient;
    context.fill();
  }

  context.beginPath();
  context.moveTo(points[0].x, points[0].y);
  for (const point of points.slice(1)) context.lineTo(point.x, point.y);
  context.strokeStyle = series.color;
  context.lineWidth = 1.5;
  context.lineJoin = 'round';
  context.stroke();
}

function toAlpha(color: string, alpha: number): string {
  // Series colors are plain "#rrggbb"; converting to rgba() here keeps the
  // input list a simple list of hex strings.
  const r = parseInt(color.slice(1, 3), 16);
  const g = parseInt(color.slice(3, 5), 16);
  const b = parseInt(color.slice(5, 7), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}
