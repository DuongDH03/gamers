import { describe, expect, it, vi } from 'vitest';
import {
  action,
  clampVolume,
  createWebViewBridge,
  LIMITS,
  parseViewMessage,
  type ViewMessage,
} from '../bridge';
import { connectBridge } from '../connect';
import { baseView, fakeWebView } from './helpers';

// The example from the spec, section 6a, as native would send it.
const SPEC_EXAMPLE = {
  type: 'view',
  screen: 'main',
  game: 'sandbox',
  hud: 'coins 3',
  paused: false,
  has_save: true,
  online: false,
  music_volume: 0.8,
  effects_volume: 1.0,
  notice: 'saved',
  commands: ['drop_coin'],
};

describe('parseViewMessage', () => {
  it('accepts the spec example as it is', () => {
    expect(parseViewMessage(SPEC_EXAMPLE)).toEqual(SPEC_EXAMPLE);
  });

  it('accepts every screen', () => {
    for (const screen of ['main', 'playing', 'paused', 'settings'] as const) {
      expect(parseViewMessage({ ...SPEC_EXAMPLE, screen })?.screen).toBe(screen);
    }
  });

  it('accepts JSON text (PostWebMessageAsString)', () => {
    expect(parseViewMessage(JSON.stringify(SPEC_EXAMPLE))).toEqual(SPEC_EXAMPLE);
  });

  it('defaults a missing or null notice and commands', () => {
    const { notice: _n, commands: _c, ...rest } = SPEC_EXAMPLE;
    expect(parseViewMessage(rest)).toEqual({ ...rest, notice: '', commands: [] });
    expect(parseViewMessage({ ...rest, notice: null, commands: null })).toEqual({
      ...rest,
      notice: '',
      commands: [],
    });
  });

  it('ignores unknown extra fields and returns a fresh object', () => {
    const input = { ...SPEC_EXAMPLE, extra: { deep: true }, commands: ['drop_coin'] };
    const out = parseViewMessage(input);
    expect(out).toEqual(SPEC_EXAMPLE);
    expect(out).not.toHaveProperty('extra');
    input.commands.push('jump');
    expect(out?.commands).toEqual(['drop_coin']);
  });

  it('accepts volumes at the ends of 0..1', () => {
    expect(parseViewMessage({ ...SPEC_EXAMPLE, music_volume: 0, effects_volume: 1 })).not.toBeNull();
  });

  it('drops repeated command names', () => {
    expect(
      parseViewMessage({ ...SPEC_EXAMPLE, commands: ['drop_coin', 'drop_coin', 'jump'] })?.commands,
    ).toEqual(['drop_coin', 'jump']);
  });

  const required = [
    'type',
    'screen',
    'game',
    'hud',
    'paused',
    'has_save',
    'online',
    'music_volume',
    'effects_volume',
  ] as const;

  it.each(required)('rejects a view without "%s"', (key) => {
    const copy: Record<string, unknown> = { ...SPEC_EXAMPLE };
    delete copy[key];
    expect(parseViewMessage(copy)).toBeNull();
  });

  const hostile: Array<[string, unknown]> = [
    ['undefined', undefined],
    ['null', null],
    ['a number', 42],
    ['a boolean', true],
    ['an array', [SPEC_EXAMPLE]],
    ['broken JSON text', '{"type":"view",'],
    ['JSON text of a number', '7'],
    ['an action echoed back', { type: 'action', name: 'play' }],
    ['an unknown type', { ...SPEC_EXAMPLE, type: 'View' }],
    ['an unknown screen', { ...SPEC_EXAMPLE, screen: 'credits' }],
    ['screen as a number', { ...SPEC_EXAMPLE, screen: 1 }],
    ['game as a number', { ...SPEC_EXAMPLE, game: 7 }],
    ['hud as an object', { ...SPEC_EXAMPLE, hud: { toString: () => 'x' } }],
    ['paused as a string', { ...SPEC_EXAMPLE, paused: 'false' }],
    ['has_save as 1', { ...SPEC_EXAMPLE, has_save: 1 }],
    ['online as null', { ...SPEC_EXAMPLE, online: null }],
    ['volume as a string', { ...SPEC_EXAMPLE, music_volume: '0.5' }],
    ['volume NaN', { ...SPEC_EXAMPLE, music_volume: Number.NaN }],
    ['volume Infinity', { ...SPEC_EXAMPLE, effects_volume: Infinity }],
    ['volume below 0', { ...SPEC_EXAMPLE, music_volume: -0.01 }],
    ['volume above 1', { ...SPEC_EXAMPLE, effects_volume: 1.5 }],
    ['notice as a number', { ...SPEC_EXAMPLE, notice: 3 }],
    ['a huge hud', { ...SPEC_EXAMPLE, hud: 'x'.repeat(LIMITS.maxHud + 1) }],
    ['a huge game name', { ...SPEC_EXAMPLE, game: 'g'.repeat(LIMITS.maxGame + 1) }],
    ['a huge notice', { ...SPEC_EXAMPLE, notice: 'n'.repeat(LIMITS.maxNotice + 1) }],
    ['commands as a string', { ...SPEC_EXAMPLE, commands: 'drop_coin' }],
    ['commands as an object', { ...SPEC_EXAMPLE, commands: { 0: 'drop_coin', length: 1 } }],
    ['a command that is a number', { ...SPEC_EXAMPLE, commands: [1] }],
    ['a command with a space', { ...SPEC_EXAMPLE, commands: ['drop coin'] }],
    ['a command with markup', { ...SPEC_EXAMPLE, commands: ['<img src=x onerror=alert(1)>'] }],
    ['a command named __proto__', { ...SPEC_EXAMPLE, commands: ['__proto__'] }],
    ['an empty command', { ...SPEC_EXAMPLE, commands: [''] }],
    ['a too long command', { ...SPEC_EXAMPLE, commands: ['a'.repeat(33)] }],
    [
      'too many commands',
      { ...SPEC_EXAMPLE, commands: Array.from({ length: LIMITS.maxCommands + 1 }, (_, i) => `c${i}`) },
    ],
    ['an inherited view (fields on the prototype)', Object.create(SPEC_EXAMPLE)],
    ['a class instance', new (class { type = 'view'; screen = 'main' })()],
    ['a Date', new Date()],
    ['JSON text over the size limit', JSON.stringify({ ...SPEC_EXAMPLE, pad: 'x'.repeat(LIMITS.maxJsonLength) })],
  ];

  it.each(hostile)('rejects %s', (_name, data) => {
    expect(parseViewMessage(data)).toBeNull();
  });

  it('does not let __proto__ in JSON pollute anything', () => {
    const text = JSON.stringify(SPEC_EXAMPLE).replace(
      '{',
      '{"__proto__":{"polluted":true,"screen":"paused"},',
    );
    const out = parseViewMessage(text);
    expect(out?.screen).toBe('main');
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
    expect(out).not.toHaveProperty('polluted');
  });
});

describe('the bridge core', () => {
  it('counts malformed messages, ignores them, and keeps the last good view', () => {
    const fake = fakeWebView();
    const bridge = createWebViewBridge(fake.webview);
    const seen: ViewMessage[] = [];
    bridge.subscribe((v) => seen.push(v));

    fake.deliver(null);
    fake.deliver({ type: 'view' });
    fake.deliver(baseView({ hud: 'coins 9' }));
    fake.deliver('not json');
    fake.deliver(baseView({ screen: 'nope' as never }));

    expect(bridge.stats).toEqual({ received: 1, rejected: 4 });
    expect(seen).toHaveLength(1);
    expect(bridge.current()?.hud).toBe('coins 9');
  });

  it('survives a message whose getters throw', () => {
    const fake = fakeWebView();
    const bridge = createWebViewBridge(fake.webview);
    const evil = { ...SPEC_EXAMPLE };
    Object.defineProperty(evil, 'hud', {
      enumerable: true,
      get() {
        throw new Error('boom');
      },
    });
    expect(() => fake.deliver(evil)).not.toThrow();
    const proxy = new Proxy(
      {},
      {
        getPrototypeOf() {
          throw new Error('trap');
        },
      },
    );
    expect(() => fake.deliver(proxy)).not.toThrow();
    expect(bridge.stats).toEqual({ received: 0, rejected: 2 });
  });

  it('a listener that unsubscribes no longer hears views', () => {
    const fake = fakeWebView();
    const bridge = createWebViewBridge(fake.webview);
    const listener = vi.fn();
    const off = bridge.subscribe(listener);
    fake.deliver(baseView());
    off();
    fake.deliver(baseView());
    expect(listener).toHaveBeenCalledTimes(1);
  });

  it('posts actions to WebView2 as objects, exactly', () => {
    const fake = fakeWebView();
    const bridge = createWebViewBridge(fake.webview);
    bridge.send(action.simple('play'));
    bridge.send(action.volume('set_music_volume', 0.5));
    bridge.send(action.command('drop_coin'));
    expect(fake.posted).toEqual([
      { type: 'action', name: 'play' },
      { type: 'action', name: 'set_music_volume', value: 0.5 },
      { type: 'action', name: 'command', command: 'drop_coin' },
    ]);
  });

  it('dispose removes the WebView2 listener', () => {
    const fake = fakeWebView();
    const bridge = createWebViewBridge(fake.webview);
    expect(fake.listenerCount()).toBe(1);
    bridge.dispose();
    expect(fake.listenerCount()).toBe(0);
  });
});

describe('connectBridge', () => {
  it('uses window.chrome.webview when it exists', () => {
    const fake = fakeWebView();
    window.chrome = { webview: fake.webview };
    const bridge = connectBridge(window);
    expect(bridge.kind).toBe('webview');
    bridge.send(action.simple('quit'));
    expect(fake.posted).toEqual([{ type: 'action', name: 'quit' }]);
    bridge.dispose();
  });

  it('falls back to the mock in a plain browser', () => {
    const bridge = connectBridge(window);
    expect(bridge.kind).toBe('mock');
    expect(bridge.current()?.screen).toBe('main');
    bridge.dispose();
  });

  it('falls back to the mock when chrome.webview is not usable', () => {
    window.chrome = { webview: {} as never };
    const bridge = connectBridge(window);
    expect(bridge.kind).toBe('mock');
    bridge.dispose();
  });
});

describe('action builders', () => {
  it('clamps and rounds volumes', () => {
    expect(clampVolume(0.555555)).toBe(0.56);
    expect(clampVolume(-1)).toBe(0);
    expect(clampVolume(7)).toBe(1);
    expect(clampVolume(Number.NaN)).toBe(0);
    expect(action.volume('set_effects_volume', 2)).toEqual({
      type: 'action',
      name: 'set_effects_volume',
      value: 1,
    });
  });
});
