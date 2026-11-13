// Keyboard and gamepad focus for menus.
//
// A menu is an element with `data-menu`; its focusable items carry
// `data-nav`. Arrow keys move between items (Left/Right adjust a slider
// instead), Home/End jump, Enter and Space activate (native button
// behaviour). A gamepad's d-pad or left stick moves, south (A / Cross)
// activates, east (B / Circle) goes back.

import { useEffect, useRef } from 'react';

export function menuRoot(doc: Document = document): HTMLElement | null {
  return doc.querySelector<HTMLElement>('[data-menu]');
}

export function navItems(root: HTMLElement): HTMLElement[] {
  return Array.from(root.querySelectorAll<HTMLElement>('[data-nav]')).filter(
    (el) => !(el as HTMLButtonElement).disabled && el.getAttribute('aria-disabled') !== 'true',
  );
}

function isRange(el: Element | null): el is HTMLInputElement {
  return el instanceof HTMLInputElement && el.type === 'range';
}

/** Moves focus by `delta` items, wrapping; from outside the menu, goes to the first. */
export function moveFocus(root: HTMLElement, delta: number): void {
  const items = navItems(root);
  if (items.length === 0) return;
  const at = items.indexOf(document.activeElement as HTMLElement);
  const next =
    at < 0 ? (delta > 0 ? 0 : items.length - 1) : (at + delta + items.length) % items.length;
  items[next]?.focus();
}

export function focusFirst(root: HTMLElement | null): void {
  if (!root) return;
  const preferred = root.querySelector<HTMLElement>('[data-autofocus]:not(:disabled)');
  const target = preferred ?? navItems(root)[0];
  target?.focus({ preventScroll: true });
}

/** Steps a slider and fires `input`, as a keyboard or pointer would. */
export function stepRange(el: HTMLInputElement, direction: 1 | -1): void {
  if (direction > 0) el.stepUp();
  else el.stepDown();
  el.dispatchEvent(new Event('input', { bubbles: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
}

/**
 * Arrow-key navigation for the menu that is on screen. Active while
 * `enabled`; focuses the menu's first item whenever `focusKey` changes.
 */
export function useMenuKeys(enabled: boolean, focusKey: unknown): void {
  useEffect(() => {
    if (enabled) focusFirst(menuRoot());
  }, [enabled, focusKey]);

  useEffect(() => {
    if (!enabled) return;
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.defaultPrevented || e.altKey || e.ctrlKey || e.metaKey) return;
      const root = menuRoot();
      if (!root) return;
      const target = document.activeElement;
      const inside = target instanceof HTMLElement && root.contains(target);
      switch (e.key) {
        case 'ArrowDown':
          moveFocus(root, 1);
          break;
        case 'ArrowUp':
          moveFocus(root, -1);
          break;
        case 'ArrowRight':
        case 'ArrowLeft':
          if (inside && isRange(target)) return; // the slider handles it
          moveFocus(root, e.key === 'ArrowRight' ? 1 : -1);
          break;
        case 'Home':
          navItems(root)[0]?.focus();
          break;
        case 'End': {
          const items = navItems(root);
          items[items.length - 1]?.focus();
          break;
        }
        default:
          return;
      }
      e.preventDefault();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [enabled]);
}

// ---------------------------------------------------------------------------
// Gamepad (browser Gamepad API, standard mapping)

const PAD = { south: 0, east: 1, up: 12, down: 13, left: 14, right: 15 } as const;
const STICK_THRESHOLD = 0.6;
const REPEAT_DELAY_MS = 400;
const REPEAT_EVERY_MS = 130;

type PadInput = 'up' | 'down' | 'left' | 'right' | 'south' | 'east';

function readPad(pad: Gamepad): Record<PadInput, boolean> {
  const pressed = (i: number) => pad.buttons[i]?.pressed ?? false;
  const x = pad.axes[0] ?? 0;
  const y = pad.axes[1] ?? 0;
  return {
    up: pressed(PAD.up) || y < -STICK_THRESHOLD,
    down: pressed(PAD.down) || y > STICK_THRESHOLD,
    left: pressed(PAD.left) || x < -STICK_THRESHOLD,
    right: pressed(PAD.right) || x > STICK_THRESHOLD,
    south: pressed(PAD.south),
    east: pressed(PAD.east),
  };
}

/** Handles one gamepad input for the menu on screen. Exported for tests. */
export function applyPadInput(input: PadInput, onBack: () => void): void {
  const root = menuRoot();
  if (input === 'east') {
    onBack();
    return;
  }
  if (!root) return;
  const active = document.activeElement;
  const inside = active instanceof HTMLElement && root.contains(active);
  switch (input) {
    case 'up':
      moveFocus(root, -1);
      break;
    case 'down':
      moveFocus(root, 1);
      break;
    case 'left':
    case 'right':
      if (inside && isRange(active)) stepRange(active, input === 'right' ? 1 : -1);
      else moveFocus(root, input === 'right' ? 1 : -1);
      break;
    case 'south':
      if (inside && active instanceof HTMLButtonElement) active.click();
      else focusFirst(root);
      break;
  }
}

/**
 * Polls the first connected gamepad while `enabled` (menus only: while
 * playing, the game reads the pad). Edges fire once; directions repeat
 * while held.
 */
export function useGamepadMenu(enabled: boolean, onBack: () => void): void {
  const onBackRef = useRef(onBack);
  onBackRef.current = onBack;

  useEffect(() => {
    if (!enabled || typeof navigator.getGamepads !== 'function') return;
    let frame = 0;
    let first = true;
    const held = new Map<PadInput, number>(); // input -> next repeat time

    const poll = (now: number) => {
      frame = requestAnimationFrame(poll);
      let pad: Gamepad | null = null;
      for (const p of navigator.getGamepads()) {
        if (p && p.connected) {
          pad = p;
          break;
        }
      }
      if (!pad) {
        held.clear();
        return;
      }
      const state = readPad(pad);
      for (const input of Object.keys(state) as PadInput[]) {
        if (!state[input]) {
          held.delete(input);
          continue;
        }
        const next = held.get(input);
        if (next === undefined) {
          // Buttons already down when the menu opened (the press that
          // opened it) don't count.
          held.set(input, first ? Infinity : now + REPEAT_DELAY_MS);
          if (!first) applyPadInput(input, () => onBackRef.current());
        } else if (now >= next && input !== 'south' && input !== 'east') {
          held.set(input, now + REPEAT_EVERY_MS);
          applyPadInput(input, () => onBackRef.current());
        }
      }
      first = false;
    };
    frame = requestAnimationFrame(poll);
    return () => cancelAnimationFrame(frame);
  }, [enabled]);
}
