/**
 * Chrome Web Serial transport → same line buffer / parser as fixture replay.
 * Disconnect = link loss (not EVT TERMINATE).
 */
import { parser, notify, resetMission } from './state.js';

let port = null;
let reader = null;
let reading = false;

export function webSerialSupported() {
  return typeof navigator !== 'undefined' && 'serial' in navigator;
}

export async function connectSerial(baudRate = 115200) {
  if (!webSerialSupported()) {
    throw new Error('Web Serial not available — use Chrome/Edge or fixture replay');
  }
  port = await navigator.serial.requestPort();
  await port.open({ baudRate });
  resetMission('uart');
  notify();
  reading = true;
  const decoder = new TextDecoder();
  // Keep reading until disconnect
  (async () => {
    try {
      while (port && reading && port.readable) {
        reader = port.readable.getReader();
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            if (value) {
              parser.pushChunk(decoder.decode(value, { stream: true }));
              notify();
            }
          }
        } finally {
          reader.releaseLock();
          reader = null;
        }
      }
    } catch (err) {
      console.warn('serial read ended', err);
    } finally {
      parser.markDisconnect();
      notify();
    }
  })();
  return port;
}

export async function disconnectSerial() {
  reading = false;
  try {
    await reader?.cancel();
  } catch (_) {
    /* ignore */
  }
  try {
    await port?.close();
  } catch (_) {
    /* ignore */
  }
  port = null;
  parser.markDisconnect();
  notify();
}
