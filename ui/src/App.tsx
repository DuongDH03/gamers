import { useCallback, useEffect, useRef, useState } from 'react';
import { action, type ActionMessage, type Bridge, type ViewMessage } from './bridge';
import { useGamepadMenu, useMenuKeys } from './nav';
import { Hud } from './screens/Hud';
import { MainMenu } from './screens/MainMenu';
import { PauseMenu } from './screens/PauseMenu';
import { Settings } from './screens/Settings';

/** How long a notice stays on screen (it fades out at the end). */
export const NOTICE_MS = 2000;

/**
 * What Esc (or the gamepad's east button in a menu) does on each screen:
 * playing -> pause (offline only), paused -> resume, settings -> back.
 */
export function backAction(view: ViewMessage | null): ActionMessage | null {
  if (!view) return null;
  switch (view.screen) {
    case 'playing':
      return view.online ? null : action.simple('pause');
    case 'paused':
      return action.simple('resume');
    case 'settings':
      return action.simple('close_settings');
    case 'main':
      return null;
  }
}

interface Toast {
  id: number;
  text: string;
}

export function App({ bridge }: { bridge: Bridge }) {
  const [view, setView] = useState<ViewMessage | null>(() => bridge.current());
  const [toast, setToast] = useState<Toast | null>(null);
  const lastNotice = useRef('');
  const toastId = useRef(0);
  const viewRef = useRef(view);
  viewRef.current = view;

  useEffect(() => {
    const onView = (v: ViewMessage) => {
      // A notice shows when it appears or changes; the same text repeated in
      // the next views is the same notice, not a new one.
      if (v.notice !== '' && v.notice !== lastNotice.current) {
        toastId.current += 1;
        setToast({ id: toastId.current, text: v.notice });
      }
      lastNotice.current = v.notice;
      setView(v);
    };
    const now = bridge.current();
    if (now) onView(now);
    return bridge.subscribe(onView);
  }, [bridge]);

  useEffect(() => {
    if (!toast) return;
    const timer = setTimeout(() => setToast(null), NOTICE_MS);
    return () => clearTimeout(timer);
  }, [toast]);

  const send = useCallback((m: ActionMessage) => bridge.send(m), [bridge]);

  const back = useCallback(() => {
    const m = backAction(viewRef.current);
    if (m) bridge.send(m);
    return m !== null;
  }, [bridge]);

  // Esc: opens the pause menu while playing, closes the pause menu or settings.
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key !== 'Escape' || e.repeat) return;
      if (back()) e.preventDefault();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [back]);

  const screen = view?.screen ?? null;
  const inMenu = screen === 'main' || screen === 'paused' || screen === 'settings';

  // While playing the game owns the keyboard: drop focus from any menu item.
  useEffect(() => {
    if (screen === 'playing' && document.activeElement instanceof HTMLElement) {
      document.activeElement.blur();
    }
  }, [screen]);

  useMenuKeys(inMenu, screen);
  useGamepadMenu(inMenu, back);

  return (
    <div className={`app app--${screen ?? 'waiting'}`} data-screen={screen ?? 'waiting'}>
      {view === null ? (
        <p className="waiting" role="status">
          Waiting for the game…
        </p>
      ) : null}
      {view?.screen === 'main' ? <MainMenu view={view} send={send} /> : null}
      {view?.screen === 'playing' ? <Hud view={view} send={send} /> : null}
      {view?.screen === 'paused' ? <PauseMenu view={view} send={send} /> : null}
      {view?.screen === 'settings' ? <Settings view={view} send={send} /> : null}
      <div className="notice-area" role="status" aria-live="polite">
        {toast ? (
          <p key={toast.id} className="notice">
            {toast.text}
          </p>
        ) : null}
      </div>
    </div>
  );
}
