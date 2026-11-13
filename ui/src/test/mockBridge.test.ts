import { afterEach, describe, expect, it, vi } from 'vitest';
import { action, type Bridge, type ViewMessage } from '../bridge';
import { commandLabel } from '../labels';
import { createMockBridge } from '../mockBridge';

let bridge: Bridge | null = null;

function mock(options: Parameters<typeof createMockBridge>[0] = {}) {
  bridge = createMockBridge({ tickMs: 0, ...options });
  const views: ViewMessage[] = [];
  bridge.subscribe((v) => views.push(v));
  const now = () => bridge!.current()!;
  return { bridge, views, now };
}

afterEach(() => {
  bridge?.dispose();
  bridge = null;
  vi.useRealTimers();
});

describe('mock bridge', () => {
  it('starts on the main menu with a valid view', () => {
    const { now, bridge: b } = mock();
    expect(now()).toMatchObject({ screen: 'main', game: 'sandbox', has_save: false, online: false });
    expect(b.stats.rejected).toBe(0);
  });

  it('plays, picks up coins over time, and drops one', () => {
    vi.useFakeTimers();
    const { bridge: b, now } = mock({ tickMs: 1000 });
    b.send(action.simple('play'));
    expect(now().screen).toBe('playing');
    vi.advanceTimersByTime(3000);
    expect(now().hud).toBe('coins 3');
    b.send(action.command('drop_coin'));
    expect(now().hud).toBe('coins 2');
    expect(now().notice).toBe('dropped a coin');
  });

  it('pauses and resumes; coins stop while paused', () => {
    vi.useFakeTimers();
    const { bridge: b, now } = mock({ tickMs: 1000 });
    b.send(action.simple('play'));
    b.send(action.simple('pause'));
    expect(now()).toMatchObject({ screen: 'paused', paused: true });
    vi.advanceTimersByTime(5000);
    expect(now().hud).toBe('coins 0');
    b.send(action.simple('resume'));
    expect(now()).toMatchObject({ screen: 'playing', paused: false });
  });

  it('saves (has_save, a one-off notice) and loads', () => {
    vi.useFakeTimers();
    const { bridge: b, now, views } = mock({ tickMs: 1000 });
    b.send(action.simple('play'));
    vi.advanceTimersByTime(2000);
    b.send(action.simple('pause'));
    b.send(action.simple('save'));
    expect(now()).toMatchObject({ has_save: true, notice: 'saved' });
    b.send(action.simple('resume'));
    expect(now().notice).toBe('');
    vi.advanceTimersByTime(3000);
    expect(now().hud).toBe('coins 5');
    b.send(action.simple('load'));
    expect(now()).toMatchObject({ hud: 'coins 2', notice: 'loaded' });
    expect(views.length).toBeGreaterThan(5);
  });

  it('continue loads the save from the main menu', () => {
    const { bridge: b, now } = mock();
    b.send(action.simple('continue'));
    expect(now().screen).toBe('main'); // no save yet
    b.send(action.simple('play'));
    b.send(action.simple('save'));
    b.send(action.simple('quit'));
    expect(now()).toMatchObject({ screen: 'main', has_save: true });
    b.send(action.simple('continue'));
    expect(now()).toMatchObject({ screen: 'playing', notice: 'loaded' });
  });

  it('settings return to where they were opened, and keep volumes', () => {
    const { bridge: b, now } = mock();
    b.send(action.simple('open_settings'));
    expect(now().screen).toBe('settings');
    b.send(action.volume('set_music_volume', 0.25));
    b.send(action.volume('set_effects_volume', 0.6));
    b.send(action.simple('close_settings'));
    expect(now()).toMatchObject({ screen: 'main', music_volume: 0.25, effects_volume: 0.6 });

    b.send(action.simple('play'));
    b.send(action.simple('pause'));
    b.send(action.simple('open_settings'));
    b.send(action.simple('close_settings'));
    expect(now().screen).toBe('paused');
  });

  it('online: pause opens the menu without pausing; no save, no load', () => {
    const { bridge: b, now } = mock({ online: true });
    b.send(action.simple('play'));
    b.send(action.simple('pause'));
    expect(now()).toMatchObject({ screen: 'paused', paused: false, online: true });
    b.send(action.simple('save'));
    b.send(action.simple('load'));
    expect(now()).toMatchObject({ screen: 'paused', online: true, has_save: false, notice: '' });
    b.send(action.simple('resume'));
    expect(now()).toMatchObject({ screen: 'playing', paused: false });
  });

  it('ignores commands it does not offer', () => {
    const { bridge: b, now } = mock();
    b.send(action.simple('play'));
    b.send(action.command('launch_rocket'));
    expect(now().notice).toBe('');
  });
});

describe('commandLabel', () => {
  it.each([
    ['drop_coin', 'Drop coin'],
    ['jump', 'Jump'],
    ['openDoor', 'Open door'],
    ['USE_ITEM', 'Use item'],
    ['fire_2', 'Fire 2'],
  ])('%s -> %s', (name, label) => {
    expect(commandLabel(name)).toBe(label);
  });
});
