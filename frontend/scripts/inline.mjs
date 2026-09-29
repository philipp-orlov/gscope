#!/usr/bin/env node
// Post-build step: folds the hashed main.js / styles.css / favicon.ico that
// `ng build` emits into dist/frontend/browser back into a single, dependency-free
// index.html under dist/frontend/single/ -- deployment then only needs one file.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const browserDir = join(process.cwd(), 'dist/frontend/browser');
const outDir = join(process.cwd(), 'dist/frontend/single');
const outFile = join(outDir, 'index.html');

let html = readFileSync(join(browserDir, 'index.html'), 'utf8');

const readAsset = (href) => readFileSync(join(browserDir, href), 'utf8');

// The <noscript> fallback duplicates the same stylesheet link for
// JS-disabled clients; drop it since the CSS below is inlined unconditionally.
html = html.replace(/<noscript>.*?<\/noscript>/gs, '');

// Inline local <link rel="stylesheet" href="...">, leave remote (fonts) links alone.
html = html.replace(/<link rel="stylesheet" href="([^"]+)"[^>]*>/g, (match, href) => {
  if (/^https?:/.test(href)) return match;
  return `<style>${readAsset(href)}</style>`;
});

// Inline local <script src="...">, preserving any other attributes (e.g. type="module").
html = html.replace(/<script((?:(?!src=)[^>])*)\ssrc="([^"]+)"((?:(?!src=)[^>])*)><\/script>/g, (match, before, src, after) => {
  if (/^https?:/.test(src)) return match;
  return `<script${before}${after}>${readAsset(src)}</script>`;
});

// Inline the favicon as a data URI so no second file is needed.
html = html.replace(/href="favicon\.ico"/, () => {
  const buf = readFileSync(join(browserDir, 'favicon.ico'));
  return `href="data:image/x-icon;base64,${buf.toString('base64')}"`;
});

mkdirSync(outDir, { recursive: true });
writeFileSync(outFile, html);
console.log(`Single-file bundle written to ${outFile} (${(Buffer.byteLength(html) / 1024).toFixed(1)} KiB)`);
