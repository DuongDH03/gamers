// The UI bridge: the JSON messages between the native app and this page.
//
// Protocol: docs/specs/2026-11-14-offline-essentials-and-ui.md, section 6a.
//   native -> UI: { type: "view", screen, game, hud, paused, has_save, online,
//                   music_volume, effects_volume, notice, commands }
//   UI -> native: { type: "action", name, value?, command? }
//
// Transport: inside WebView2, `window.chrome.webview` (native posts with
// PostWebMessageAsJson, so `event.data` is already a parsed object; the page
// posts objects with `postMessage`, read natively with get_WebMessageAsJson).
// In a normal browser there is no `chrome.webview`, and a mock bridge that
// simulates the game is used instead (see mockBridge.ts).
//
// Every incoming message is validated at run time. Anything that doesn't
// match the shape exactly is ignored and counted, never trusted.

// ---------------------------------------------------------------------------
// Message shapes

export const SCREENS = ['main', 'playing', 'paused', 'settings'] as const;
export type Screen = (typeof SCREENS)[number];

/** Native -> UI. Sent when something changed, not every frame. */
export interface ViewMessage {
  type: 'view';
  screen: Screen;
  game: string;
  hud: string;
  paused: boolean;
  has_save: boolean;
  online: boolean;
  music_volume: number;
  effects_volume: number;
  /** A one-off message ("saved"). Empty when there is none. */
  notice: string;
  /** Game commands the UI may offer as buttons (GameApi::ui_commands). */
  commands: string[];
}

/** App actions with no payload. */
export type SimpleActionName =
  | 'play'
  | 'continue'
  | 'quit'
  | 'pause'
  | 'resume'
  | 'save'
  | 'load'
  | 'open_settings'
  | 'close_settings';

export type VolumeActionName = 'set_music_volume' | 'set_effects_volume';

/** UI -> native. */
export type ActionMessage =
  | { type: 'action'; name: SimpleActionName }
  | { type: 'action'; name: VolumeActionName; value: number }
  | { type: 'action'; name: 'command'; command: string };

export type ActionName = ActionMessage['name'];

/** Builders, so every action the UI sends has exactly the 6a shape. */
export const action = {
  simple(name: SimpleActionName): ActionMessage {
    return { type: 'action', name };
  },
  volume(name: VolumeActionName, value: number): ActionMessage {
    return { type: 'action', name, value: clampVolume(value) };
  },
  command(command: string): ActionMessage {
    return { type: 'action', name: 'command', command };
  },
};

/** 0..1, rounded to 1/100 so the JSON stays short ("0.55", not 0.550000001). */
export function clampVolume(value: number): number {
  if (!Number.isFinite(value)) return 0;
  const clamped = Math.min(1, Math.max(0, value));
  return Math.round(clamped * 100) / 100;
}

// ---------------------------------------------------------------------------
// Validation

export const LIMITS = {
  /** Largest string message accepted (if a message arrives as JSON text). */
  maxJsonLength: 16 * 1024,
  maxGame: 64,
  maxHud: 256,
  maxNotice: 256,
  maxCommands: 16,
} as const;

/** A command name: what GameApi::ui_commands names, e.g. `drop_coin`. */
export const COMMAND_NAME = /^[A-Za-z][A-Za-z0-9_]{0,31}$/;

function isPlainObject(x: unknown): x is Record<string, unknown> {
  if (typeof x !== 'object' || x === null || Array.isArray(x)) return false;
  const proto: unknown = Object.getPrototypeOf(x);
  return proto === Object.prototype || proto === null;
}

function own(obj: Record<string, unknown>, key: string): unknown {
  return Object.prototype.hasOwnProperty.call(obj, key) ? obj[key] : undefined;
}

function isBoundedString(x: unknown, max: number): x is string {
  return typeof x === 'string' && x.length <= max;
}

function isVolume(x: unknown): x is number {
  return typeof x === 'number' && Number.isFinite(x) && x >= 0 && x <= 1;
}

function isScreen(x: unknown): x is Screen {
  return typeof x === 'string' && (SCREENS as readonly string[]).includes(x);
}

/**
 * Turns whatever arrived into a ViewMessage, or null if it isn't one.
 *
 * Required: type "view", screen, game, hud, paused, has_save, online,
 * music_volume, effects_volume. Optional: notice (string, default ""),
 * commands (array of command names, default []). Unknown extra fields are
 * ignored. The result is a fresh object: nothing of the input is kept.
 */
export function parseViewMessage(data: unknown): ViewMessage | null {
  let msg: unknown = data;
  if (typeof msg === 'string') {
    if (msg.length > LIMITS.maxJsonLength) return null;
    try {
      msg = JSON.parse(msg);
    } catch {
      return null;
    }
  }
  if (!isPlainObject(msg)) return null;

  const type = own(msg, 'type');
  const screen = own(msg, 'screen');
  const game = own(msg, 'game');
  const hud = own(msg, 'hud');
  const paused = own(msg, 'paused');
  const hasSave = own(msg, 'has_save');
  const online = own(msg, 'online');
  const musicVolume = own(msg, 'music_volume');
  const effectsVolume = own(msg, 'effects_volume');
  const notice = own(msg, 'notice');
  const commands = own(msg, 'commands');

  if (type !== 'view') return null;
  if (!isScreen(screen)) return null;
  if (!isBoundedString(game, LIMITS.maxGame)) return null;
  if (!isBoundedString(hud, LIMITS.maxHud)) return null;
  if (typeof paused !== 'boolean') return null;
  if (typeof hasSave !== 'boolean') return null;
  if (typeof online !== 'boolean') return null;
  if (!isVolume(musicVolume) || !isVolume(effectsVolume)) return null;

  let noticeText = '';
  if (notice !== undefined && notice !== null) {
    if (!isBoundedString(notice, LIMITS.maxNotice)) return null;
    noticeText = notice;
  }

  const commandList: string[] = [];
  if (commands !== undefined && commands !== null) {
    if (!Array.isArray(commands) || commands.length > LIMITS.maxCommands) return null;
    for (const c of commands as unknown[]) {
      if (typeof c !== 'string' || !COMMAND_NAME.test(c)) return null;
      if (!commandList.includes(c)) commandList.push(c);
    }
  }

  return {
    type: 'view',
    screen,
    game,
    hud,
    paused,
    has_save: hasSave,
    online,
    music_volume: musicVolume,
    effects_volume: effectsVolume,
    notice: noticeText,
    commands: commandList,
  };
}

// ---------------------------------------------------------------------------
// The bridge

export type ViewListener = (view: ViewMessage) => void;

export interface BridgeStats {
  /** Valid view messages received. */
  received: number;
  /** Messages ignored because they were malformed or unknown. */
  rejected: number;
}

export interface Bridge {
  readonly kind: 'webview' | 'mock';
  send(message: ActionMessage): void;
  /** Calls `listener` for every valid view; returns an unsubscribe function. */
  subscribe(listener: ViewListener): () => void;
  /** The last valid view, or null before the first one. */
  current(): ViewMessage | null;
  readonly stats: Readonly<BridgeStats>;
  dispose(): void;
}

/** The part of WebView2's `window.chrome.webview` this page uses. */
export interface WebViewLike {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', listener: (event: { data: unknown }) => void): void;
  removeEventListener(type: 'message', listener: (event: { data: unknown }) => void): void;
}

declare global {
  interface Window {
    chrome?: { webview?: WebViewLike };
    /** The canvas the backend draws the world into (see main.tsx). */
    scrapsCanvas?: HTMLCanvasElement;
  }
}

/**
 * The shared part of both bridges: validation, counting and listeners.
 * `deliver` is fed raw incoming data from the transport.
 */
export function createBridgeCore(kind: Bridge['kind'], post: (m: ActionMessage) => void) {
  const listeners = new Set<ViewListener>();
  const stats: BridgeStats = { received: 0, rejected: 0 };
  let last: ViewMessage | null = null;

  const bridge: Bridge = {
    kind,
    send(message) {
      post(message);
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    current() {
      return last;
    },
    stats,
    dispose() {
      listeners.clear();
    },
  };

  function deliver(data: unknown): void {
    let view: ViewMessage | null = null;
    try {
      view = parseViewMessage(data);
    } catch {
      view = null; // a hostile getter or proxy: still just "malformed"
    }
    if (view === null) {
      stats.rejected += 1;
      return;
    }
    stats.received += 1;
    last = view;
    for (const listener of [...listeners]) listener(view);
  }

  return { bridge, deliver };
}

/** The WebView2 transport. */
export function createWebViewBridge(webview: WebViewLike): Bridge {
  const { bridge, deliver } = createBridgeCore('webview', (m) => webview.postMessage(m));
  const onMessage = (event: { data: unknown }) => deliver(event.data);
  webview.addEventListener('message', onMessage);
  const dispose = bridge.dispose;
  bridge.dispose = () => {
    webview.removeEventListener('message', onMessage);
    dispose();
  };
  return bridge;
}

/** The WebView2 object, if this page runs inside WebView2. */
export function findWebView(win: Window): WebViewLike | null {
  try {
    const webview = win.chrome?.webview;
    if (webview && typeof webview.postMessage === 'function') return webview;
  } catch {
    // fall through to the mock
  }
  return null;
}
