/**
 * Build + run the HAL-free radar / tracker / laser-aim regression with the
 * host C compiler (gcc on PATH, or CC=...). Run: node scripts/test_radar_aim_host.mjs
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cc = process.env.CC || 'gcc';
const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'radar-aim-')), process.platform === 'win32' ? 'radar_aim_test.exe' : 'radar_aim_test');
const src = ['App/Src/rd03d.c', 'App/Src/radar_track.c', 'App/Src/laser_aim.c', 'scripts/host/radar_aim_host_test.c'].map((p) => path.join(root, p));

execFileSync(cc, ['-std=c99', '-Wall', '-Wextra', '-Werror', '-O1', `-I${path.join(root, 'App/Inc')}`, ...src, '-lm', '-o', out], { stdio: 'inherit' });
execFileSync(out, { stdio: 'inherit' });
