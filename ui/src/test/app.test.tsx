import { act, fireEvent, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { App, NOTICE_MS } from '../App';
import { applyPadInput } from '../nav';
import { createMockBridge } from '../mockBridge';
import { renderApp } from './helpers';

afterEach(() => {
  vi.useRealTimers();
});

const esc = () => fireEvent.keyDown(window, { key: 'Escape' });
const button = (name: string) => screen.getByRole('button', { name });
const queryButton = (name: string) => screen.queryByRole('button', { name });

describe('before the first view', () => {
  it('waits, and shows no menu', () => {
    renderApp();
    expect(screen.getByText(/waiting for the game/i)).toBeInTheDocument();
    expect(screen.queryAllByRole('button')).toHaveLength(0);
  });
});

describe('main menu', () => {
  it('renders the game name and its buttons', () => {
    renderApp({ screen: 'main', game: 'sandbox' });
    expect(screen.getByRole('heading', { level: 1, name: 'sandbox' })).toBeInTheDocument();
    for (const name of ['Play', 'Continue', 'Settings', 'Quit']) {
      expect(button(name)).toBeInTheDocument();
    }
  });

  it('enables Continue only with a save', () => {
    const app = renderApp({ screen: 'main', has_save: false });
    expect(button('Continue')).toBeDisabled();
    app.show({ screen: 'main', has_save: true });
    expect(button('Continue')).toBeEnabled();
  });

  it('each button sends its exact action', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'main', has_save: true });
    await user.click(button('Play'));
    await user.click(button('Continue'));
    await user.click(button('Settings'));
    await user.click(button('Quit'));
    expect(app.posted).toEqual([
      { type: 'action', name: 'play' },
      { type: 'action', name: 'continue' },
      { type: 'action', name: 'open_settings' },
      { type: 'action', name: 'quit' },
    ]);
  });

  it('a disabled Continue sends nothing', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'main', has_save: false });
    await user.click(button('Continue'));
    expect(app.posted).toEqual([]);
  });

  it('focuses Play, or Continue when there is a save', () => {
    const app = renderApp({ screen: 'main', has_save: false });
    expect(button('Play')).toHaveFocus();
    app.unmount();
    renderApp({ screen: 'main', has_save: true });
    expect(button('Continue')).toHaveFocus();
  });

  it('arrow keys move between enabled buttons, wrapping; Enter activates', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'main', has_save: false });
    expect(button('Play')).toHaveFocus();
    await user.keyboard('{ArrowDown}');
    expect(button('Settings')).toHaveFocus(); // Continue is disabled: skipped
    await user.keyboard('{ArrowDown}');
    expect(button('Quit')).toHaveFocus();
    await user.keyboard('{ArrowDown}');
    expect(button('Play')).toHaveFocus();
    await user.keyboard('{ArrowUp}');
    expect(button('Quit')).toHaveFocus();
    await user.keyboard('{Home}');
    expect(button('Play')).toHaveFocus();
    await user.keyboard('{ArrowRight}{Enter}');
    expect(app.posted).toEqual([{ type: 'action', name: 'open_settings' }]);
  });

  it('Esc does nothing on the main menu', () => {
    const app = renderApp({ screen: 'main' });
    esc();
    expect(app.posted).toEqual([]);
  });
});

describe('playing (HUD)', () => {
  it('shows the hud text and the game name', () => {
    renderApp({ screen: 'playing', hud: 'coins 7', game: 'sandbox' });
    expect(screen.getByTestId('hud-text')).toHaveTextContent('coins 7');
    expect(screen.getByLabelText('Game HUD')).toHaveTextContent('sandbox');
  });

  it('updates the hud when a new view arrives', () => {
    const app = renderApp({ screen: 'playing', hud: 'coins 1' });
    app.show({ screen: 'playing', hud: 'coins 2' });
    expect(screen.getByTestId('hud-text')).toHaveTextContent('coins 2');
  });

  it('the pause button sends pause', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'playing' });
    await user.click(button('Pause'));
    expect(app.posted).toEqual([{ type: 'action', name: 'pause' }]);
  });

  it('has a readable button for each command, sending it', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'playing', commands: ['drop_coin', 'open_door'] });
    const group = screen.getByRole('group', { name: 'Game actions' });
    expect(within(group).getAllByRole('button').map((b) => b.textContent)).toEqual([
      'Drop coin',
      'Open door',
    ]);
    await user.click(button('Drop coin'));
    await user.click(button('Open door'));
    expect(app.posted).toEqual([
      { type: 'action', name: 'command', command: 'drop_coin' },
      { type: 'action', name: 'command', command: 'open_door' },
    ]);
  });

  it('shows no command bar without commands', () => {
    renderApp({ screen: 'playing', commands: [] });
    expect(screen.queryByRole('group', { name: 'Game actions' })).toBeNull();
  });

  it('HUD controls never keep the keyboard', async () => {
    const user = userEvent.setup();
    renderApp({ screen: 'playing' });
    const drop = button('Drop coin');
    expect(drop).toHaveAttribute('tabindex', '-1');
    expect(button('Pause')).toHaveAttribute('tabindex', '-1');
    await user.click(drop);
    expect(drop).not.toHaveFocus();
    expect(document.activeElement).toBe(document.body);
  });

  it('drops menu focus when the game starts', () => {
    const app = renderApp({ screen: 'main' });
    expect(button('Play')).toHaveFocus();
    app.show({ screen: 'playing' });
    expect(document.activeElement).toBe(document.body);
  });

  it('arrow keys are left to the game', () => {
    renderApp({ screen: 'playing' });
    const ev = new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true, cancelable: true });
    window.dispatchEvent(ev);
    expect(ev.defaultPrevented).toBe(false);
  });

  it('Esc sends pause', () => {
    const app = renderApp({ screen: 'playing' });
    esc();
    expect(app.posted).toEqual([{ type: 'action', name: 'pause' }]);
  });

  it('a held Esc (key repeat) sends nothing more', () => {
    const app = renderApp({ screen: 'playing' });
    fireEvent.keyDown(window, { key: 'Escape', repeat: true });
    expect(app.posted).toEqual([]);
  });
});

describe('notice', () => {
  it('shows, then is gone after about 2 s', () => {
    vi.useFakeTimers();
    const app = renderApp({ screen: 'paused', notice: 'saved' });
    expect(screen.getByText('saved')).toBeInTheDocument();
    act(() => vi.advanceTimersByTime(NOTICE_MS - 100));
    expect(screen.getByText('saved')).toBeInTheDocument();
    act(() => vi.advanceTimersByTime(200));
    expect(screen.queryByText('saved')).toBeNull();
    // The same text in the next view is the same notice, not a new one.
    app.show({ screen: 'paused', notice: 'saved' });
    expect(screen.queryByText('saved')).toBeNull();
    // After it was cleared, it can come again.
    app.show({ screen: 'paused', notice: '' });
    app.show({ screen: 'paused', notice: 'saved' });
    expect(screen.getByText('saved')).toBeInTheDocument();
  });

  it('a new notice replaces the old one', () => {
    vi.useFakeTimers();
    const app = renderApp({ screen: 'playing', notice: 'saved' });
    app.show({ screen: 'playing', notice: 'loaded' });
    expect(screen.queryByText('saved')).toBeNull();
    expect(screen.getByText('loaded')).toBeInTheDocument();
  });
});

describe('pause menu', () => {
  it('renders its buttons', () => {
    renderApp({ screen: 'paused', paused: true, has_save: true });
    expect(screen.getByRole('heading', { name: 'Paused' })).toBeInTheDocument();
    for (const name of ['Resume', 'Save', 'Load', 'Settings', 'Quit']) {
      expect(button(name)).toBeEnabled();
    }
    expect(button('Resume')).toHaveFocus();
  });

  it('enables Load only with a save', () => {
    renderApp({ screen: 'paused', has_save: false });
    expect(button('Load')).toBeDisabled();
    expect(button('Save')).toBeEnabled();
  });

  it('each button sends its exact action', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'paused', paused: true, has_save: true });
    for (const name of ['Resume', 'Save', 'Load', 'Settings', 'Quit']) {
      await user.click(button(name));
    }
    expect(app.posted).toEqual([
      { type: 'action', name: 'resume' },
      { type: 'action', name: 'save' },
      { type: 'action', name: 'load' },
      { type: 'action', name: 'open_settings' },
      { type: 'action', name: 'quit' },
    ]);
  });

  it('Esc sends resume', () => {
    const app = renderApp({ screen: 'paused' });
    esc();
    expect(app.posted).toEqual([{ type: 'action', name: 'resume' }]);
  });
});

describe('settings', () => {
  it('shows both volumes from the view', () => {
    renderApp({ screen: 'settings', music_volume: 0.8, effects_volume: 0.35 });
    expect(screen.getByRole('slider', { name: 'Music' })).toHaveValue('0.8');
    expect(screen.getByRole('slider', { name: 'Effects' })).toHaveValue('0.35');
    expect(screen.getByText('80%')).toBeInTheDocument();
    expect(screen.getByText('35%')).toBeInTheDocument();
    expect(screen.getByRole('slider', { name: 'Music' })).toHaveFocus();
  });

  it('moving a slider sends the volume with its value', () => {
    const app = renderApp({ screen: 'settings', music_volume: 0.8, effects_volume: 1 });
    fireEvent.change(screen.getByRole('slider', { name: 'Music' }), { target: { value: '0.35' } });
    fireEvent.change(screen.getByRole('slider', { name: 'Effects' }), { target: { value: '0' } });
    expect(app.posted).toEqual([
      { type: 'action', name: 'set_music_volume', value: 0.35 },
      { type: 'action', name: 'set_effects_volume', value: 0 },
    ]);
    expect(screen.getByText('35%')).toBeInTheDocument();
  });

  it('follows the volume native reports', () => {
    const app = renderApp({ screen: 'settings', music_volume: 0.8 });
    app.show({ screen: 'settings', music_volume: 0.2 });
    expect(screen.getByRole('slider', { name: 'Music' })).toHaveValue('0.2');
  });

  it('Left/Right stay on the slider; Up/Down move between items', async () => {
    const user = userEvent.setup();
    renderApp({ screen: 'settings' });
    const music = screen.getByRole('slider', { name: 'Music' });
    expect(music).toHaveFocus();
    await user.keyboard('{ArrowLeft}');
    expect(music).toHaveFocus();
    await user.keyboard('{ArrowDown}');
    expect(screen.getByRole('slider', { name: 'Effects' })).toHaveFocus();
    await user.keyboard('{ArrowDown}');
    expect(button('Back')).toHaveFocus();
  });

  it('Back sends close_settings', async () => {
    const user = userEvent.setup();
    const app = renderApp({ screen: 'settings' });
    await user.click(button('Back'));
    expect(app.posted).toEqual([{ type: 'action', name: 'close_settings' }]);
  });

  it('Esc sends close_settings', () => {
    const app = renderApp({ screen: 'settings' });
    esc();
    expect(app.posted).toEqual([{ type: 'action', name: 'close_settings' }]);
  });
});

describe('online', () => {
  it('hides pause on the HUD, and Esc does not pause', () => {
    const app = renderApp({ screen: 'playing', online: true });
    expect(queryButton('Pause')).toBeNull();
    expect(screen.getByText('online')).toBeInTheDocument();
    expect(button('Drop coin')).toBeInTheDocument();
    esc();
    expect(app.posted).toEqual([]);
  });

  it('hides Save and Load in the pause menu', () => {
    renderApp({ screen: 'paused', online: true, has_save: true });
    expect(queryButton('Save')).toBeNull();
    expect(queryButton('Load')).toBeNull();
    expect(button('Resume')).toBeInTheDocument();
  });

  it('hides Continue (a load) on the main menu', () => {
    renderApp({ screen: 'main', online: true, has_save: true });
    expect(queryButton('Continue')).toBeNull();
    expect(button('Play')).toHaveFocus();
  });
});

describe('hostile input from native', () => {
  it('a malformed message changes nothing and is counted', () => {
    const app = renderApp({ screen: 'playing', hud: 'coins 4' });
    app.deliverRaw({ type: 'view', screen: 'main' });
    app.deliverRaw('<script>alert(1)</script>');
    app.deliverRaw({ ...{ type: 'view' }, screen: 'paused', game: 1 });
    expect(screen.getByTestId('hud-text')).toHaveTextContent('coins 4');
    expect(app.bridge.stats.rejected).toBe(3);
  });

  it('text is shown as text, never as markup', () => {
    renderApp({ screen: 'playing', hud: '<img src=x onerror="alert(1)">' });
    expect(screen.getByTestId('hud-text').textContent).toBe('<img src=x onerror="alert(1)">');
    expect(document.querySelector('img')).toBeNull();
  });
});

describe('gamepad in menus', () => {
  it('d-pad moves, south activates, east goes back', () => {
    const app = renderApp({ screen: 'paused', has_save: true });
    const back = vi.fn();
    expect(button('Resume')).toHaveFocus();
    applyPadInput('down', back);
    expect(button('Save')).toHaveFocus();
    applyPadInput('south', back);
    expect(app.posted).toEqual([{ type: 'action', name: 'save' }]);
    applyPadInput('east', back);
    expect(back).toHaveBeenCalledTimes(1);
  });

  it('left/right on a slider step the volume', () => {
    const app = renderApp({ screen: 'settings', music_volume: 0.5 });
    applyPadInput('right', () => {});
    expect(app.posted).toEqual([{ type: 'action', name: 'set_music_volume', value: 0.55 }]);
  });
});

describe('with the mock game', () => {
  it('plays a whole round: play, pause with Esc, save, resume, quit, continue', async () => {
    const user = userEvent.setup();
    const bridge = createMockBridge({ tickMs: 0 });
    render(<App bridge={bridge} />);
    await user.click(button('Play'));
    expect(screen.getByTestId('hud-text')).toHaveTextContent('coins 0');
    esc();
    await user.click(button('Save'));
    expect(screen.getByText('saved')).toBeInTheDocument();
    await user.click(button('Resume'));
    esc();
    await user.click(button('Quit'));
    expect(button('Continue')).toBeEnabled();
    await user.click(button('Continue'));
    expect(screen.getByTestId('hud-text')).toBeInTheDocument();
    bridge.dispose();
  });
});
