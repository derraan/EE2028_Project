/**
 * Build + run the HAL-free Wi-Fi telemetry regression (ism43362.c + wifi_link.c
 * against a fake ISM43362) with the host C compiler (gcc on PATH, or CC=...).
 * Run: node scripts/test_wifi_host.mjs
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cc = process.env.CC || 'gcc';
const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'wifi-')), process.platform === 'win32' ? 'wifi_test.exe' : 'wifi_test');
const src = ['App/Src/ism43362.c', 'App/Src/wifi_link.c', 'scripts/host/wifi_host_test.c'].map((p) => path.join(root, p));

execFileSync(cc, ['-std=c99', '-Wall', '-Wextra', '-O1', `-I${path.join(root, 'App/Inc')}`, ...src, '-o', out], { stdio: 'inherit' });
execFileSync(out, { stdio: 'inherit' });
