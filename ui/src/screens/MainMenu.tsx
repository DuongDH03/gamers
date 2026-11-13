import { action } from '../bridge';
import { KeyHints, MenuButton, type ScreenProps } from './common';

export function MainMenu({ view, send }: ScreenProps) {
  const canContinue = view.has_save && !view.online;
  return (
    <section className="screen screen--main" data-menu="" aria-labelledby="main-title">
      <div className="main-layout">
        <header className="brand">
          <p className="brand__kicker">a Scraps game</p>
          <h1 id="main-title" className="brand__title">
            {view.game || 'Scraps'}
          </h1>
          <span className="brand__rule" aria-hidden="true" />
        </header>
        <nav className="menu-list" aria-label="Main menu">
          <MenuButton autoFocusHere={!canContinue} onClick={() => send(action.simple('play'))}>
            Play
          </MenuButton>
          {view.online ? null : (
            <MenuButton
              autoFocusHere={canContinue}
              disabled={!canContinue}
              onClick={() => send(action.simple('continue'))}
            >
              Continue
            </MenuButton>
          )}
          <MenuButton onClick={() => send(action.simple('open_settings'))}>Settings</MenuButton>
          <MenuButton className="menu-button--quiet" onClick={() => send(action.simple('quit'))}>
            Quit
          </MenuButton>
        </nav>
      </div>
      <KeyHints />
    </section>
  );
}
