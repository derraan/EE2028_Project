/**
 * Build + run the matrix HUD / buzzer integration regression with the host C
 * compiler (gcc on PATH, or CC=...). Uses scripts/host/stub/stm32l4xx_hal.h
 * in place of the real HAL. Run: node scripts/test_hud_buzzer_host.mjs
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cc = process.env.CC || 'gcc';
const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'hud-buzzer-')), process.platform === 'win32' ? 'hud_buzzer_test.exe' : 'hud_buzzer_test');
const src = [
  'App/Src/enhancements.c',
  'App/Src/status_display.c',
  'App/Src/ht16k33.c',
  'App/Src/buzzer.c',
  'App/Src/ir_threat.c',
  'App/Src/relay_effect.c',
  'App/Src/telemetry_evt.c',
  'App/Src/rd03d.c',
  'App/Src/radar_track.c',
  'App/Src/laser_aim.c',
  'scripts/host/hud_buzzer_host_test.c',
].map((p) => path.join(root, p));

execFileSync(cc, ['-std=c99', '-Wall', '-Wextra', '-O1',
  `-I${path.join(root, 'scripts/host/stub')}`, `-I${path.join(root, 'App/Inc')}`, `-I${path.join(root, 'Core/Inc')}`,
  ...src, '-lm', '-o', out], { stdio: 'inherit' });
execFileSync(out, { stdio: 'inherit' });
