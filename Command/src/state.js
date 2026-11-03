/** Shared dashboard state holder + subscribers. */
import { createEmptyState, createParser } from './parser.js';

const listeners = new Set();
export const parser = createParser(createEmptyState());

export function getState() {
  return parser.state;
}

export function subscribe(fn) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export function notify() {
  const s = parser.state;
  for (const fn of listeners) fn(s);
}

export function resetMission(link = 'idle') {
  parser.reset();
  parser.markLink(link);
  notify();
}
