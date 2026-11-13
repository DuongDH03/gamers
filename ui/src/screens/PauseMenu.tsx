import { action } from '../bridge';
import { KeyHints, MenuButton, type ScreenProps } from './common';

export function PauseMenu({ view, send }: ScreenProps) {
  return (
    <section className="screen screen--overlay" data-menu="" aria-labelledby="pause-title">
      <div className="panel">
        <header className="panel__header">
          {/* Online the game runs on behind the menu. */}
          <h2 id="pause-title" className="panel__title">
            {view.online ? 'Menu' : 'Paused'}
          </h2>
          <p className="panel__sub">{view.hud}</p>
        </header>
        <nav className="menu-list" aria-label="Pause menu">
          <MenuButton autoFocusHere onClick={() => send(action.simple('resume'))}>
            Resume
          </MenuButton>
          {view.online ? null : (
            <>
              <MenuButton onClick={() => send(action.simple('save'))}>Save</MenuButton>
              <MenuButton disabled={!view.has_save} onClick={() => send(action.simple('load'))}>
                Load
              </MenuButton>
            </>
          )}
          <MenuButton onClick={() => send(action.simple('open_settings'))}>Settings</MenuButton>
          <MenuButton className="menu-button--quiet" onClick={() => send(action.simple('quit'))}>
            Quit
          </MenuButton>
        </nav>
      </div>
      <KeyHints back="resume" />
    </section>
  );
}
