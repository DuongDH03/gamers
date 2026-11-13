// A pretend game for working on the UI in a normal browser (`npm run dev`).
// It answers actions the way the native app does: coins go up while playing,
// pause and resume, save and load with notices, has_save after a save, and
// volumes. Its view messages go through the same validation as real ones.

import {
  clampVolume,
  createBridgeCore,
  type ActionMessage,
  type Bridge,
  type Screen,
  type ViewMessage,
} from './bridge';

export interface MockOptions {
  game?: string;
  online?: boolean;
  /** Milliseconds between coin pickups while playing; 0 turns the clock off. */
  tickMs?: number;
  commands?: string[];
}

export function createMockBridge(options: MockOptions = {}): Bridge {
  const online = options.online ?? false;
  const tickMs = options.tickMs ?? 1200;
  const state = {
    game: options.game ?? 'sandbox',
    commands: options.commands ?? ['drop_coin'],
    screen: 'main' as Screen,
    settingsReturn: 'main' as Screen,
    coins: 0,
    savedCoins: null as number | null,
    music: 0.8,
    effects: 1.0,
    notice: '',
  };

  const { bridge, deliver } = createBridgeCore('mock', (m) => handle(m));

  function view(): ViewMessage {
    return {
      type: 'view',
      screen: state.screen,
      game: state.game,
      hud: `coins ${state.coins}`,
      paused: state.screen === 'paused',
      has_save: state.savedCoins !== null,
      online,
      music_volume: state.music,
      effects_volume: state.effects,
      notice: state.notice,
      commands: state.commands,
    };
  }

  function emit(): void {
    // A JSON round trip, as through PostWebMessageAsJson.
    deliver(JSON.parse(JSON.stringify(view())));
    state.notice = '';
  }

  function inGame(): boolean {
    return state.screen === 'playing' || state.screen === 'paused';
  }

  function handle(m: ActionMessage): void {
    switch (m.name) {
      case 'play':
        state.coins = 0;
        state.screen = 'playing';
        break;
      case 'continue':
        if (online || state.savedCoins === null) return;
        state.coins = state.savedCoins;
        state.screen = 'playing';
        state.notice = 'loaded';
        break;
      case 'pause':
        if (online || state.screen !== 'playing') return;
        state.screen = 'paused';
        break;
      case 'resume':
        if (state.screen !== 'paused') return;
        state.screen = 'playing';
        break;
      case 'save':
        if (online || !inGame()) return;
        state.savedCoins = state.coins;
        state.notice = 'saved';
        break;
      case 'load':
        if (online || !inGame() || state.savedCoins === null) return;
        state.coins = state.savedCoins;
        state.notice = 'loaded';
        break;
      case 'open_settings':
        if (state.screen !== 'main' && state.screen !== 'paused') return;
        state.settingsReturn = state.screen;
        state.screen = 'settings';
        break;
      case 'close_settings':
        if (state.screen !== 'settings') return;
        state.screen = state.settingsReturn;
        break;
      case 'set_music_volume':
        state.music = clampVolume(m.value);
        break;
      case 'set_effects_volume':
        state.effects = clampVolume(m.value);
        break;
      case 'command':
        if (state.screen !== 'playing' || !state.commands.includes(m.command)) return;
        if (m.command === 'drop_coin') {
          if (state.coins > 0) {
            state.coins -= 1;
            state.notice = 'dropped a coin';
          } else {
            state.notice = 'no coins to drop';
          }
        } else {
          state.notice = m.command;
        }
        break;
      case 'quit':
        if (inGame()) {
          state.screen = 'main';
        } else {
          state.notice = 'quit: the real app closes here';
        }
        break;
    }
    emit();
  }

  let timer: ReturnType<typeof setInterval> | null = null;
  if (tickMs > 0) {
    timer = setInterval(() => {
      if (state.screen !== 'playing') return;
      state.coins += 1;
      emit();
    }, tickMs);
  }

  const dispose = bridge.dispose;
  bridge.dispose = () => {
    if (timer !== null) clearInterval(timer);
    timer = null;
    dispose();
  };

  emit(); // the first view, as native sends after the page loads
  return bridge;
}
