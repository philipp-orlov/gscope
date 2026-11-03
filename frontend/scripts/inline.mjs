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

const MIME_TYPES = { '.woff2': 'font/woff2', '.woff': 'font/woff', '.ico': 'image/x-icon' };
const dataUri = (href) => {
  const type = MIME_TYPES[href.slice(href.lastIndexOf('.'))];
  if (!type) throw new Error(`no MIME type for ${href}`);
  return `data:${type};base64,${readFileSync(join(browserDir, href)).toString('base64')}`;
};

// The bundled fonts (src/fonts.scss) the stylesheet refers to become data URIs, so the CSS needs no second file.
const inlineFonts = (css) => css.replace(/url\("?(\.\/media\/[^")]+)"?\)/g, (match, href) => `url("${dataUri(href)}")`);

// The <noscript> fallback duplicates the same stylesheet link for
// JS-disabled clients; drop it since the CSS below is inlined unconditionally.
html = html.replace(/<noscript>.*?<\/noscript>/gs, '');

// The critical CSS that ng build already put in a <style> (inlineCritical) refers to the same fonts.
html = html.replace(/<style>([\s\S]*?)<\/style>/g, (match, css) => `<style>${inlineFonts(css)}</style>`);

// Inline local <link rel="stylesheet" href="...">, leave remote links alone.
html = html.replace(/<link rel="stylesheet" href="([^"]+)"[^>]*>/g, (match, href) => {
  if (/^https?:/.test(href)) return match;
  return `<style>${inlineFonts(readAsset(href))}</style>`;
});

// Inline local <script src="...">, preserving any other attributes (e.g. type="module").
// A script may not contain "</script" (it would end the element early); in JavaScript "<\/script" means the same.
html = html.replace(/<script((?:(?!src=)[^>])*)\ssrc="([^"]+)"((?:(?!src=)[^>])*)><\/script>/g, (match, before, src, after) => {
  if (/^https?:/.test(src)) return match;
  return `<script${before}${after}>${readAsset(src).replace(/<\/script/gi, '<\\/script')}</script>`;
});

// Inline the favicon as a data URI so no second file is needed.
html = html.replace(/href="favicon\.ico"/, () => {
  return `href="${dataUri('favicon.ico')}"`;
});

mkdirSync(outDir, { recursive: true });
writeFileSync(outFile, html);
console.log(`Single-file bundle written to ${outFile} (${(Buffer.byteLength(html) / 1024).toFixed(1)} KiB)`);
