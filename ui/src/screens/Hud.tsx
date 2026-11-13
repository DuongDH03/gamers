import type { MouseEvent, ReactNode } from 'react';
import { action } from '../bridge';
import { commandLabel } from '../labels';
import type { ScreenProps } from './common';

/**
 * A HUD control. While playing, the game owns the keyboard: these buttons
 * never take focus (no tab stop, no focus on mouse down), so Space or Enter
 * after a click still go to the game.
 */
function HudButton({
  label,
  className,
  onPress,
  children,
}: {
  label: string;
  className?: string;
  onPress: () => void;
  children?: ReactNode;
}) {
  return (
    <button
      type="button"
      tabIndex={-1}
      aria-label={label}
      className={className ? `hud-button ${className}` : 'hud-button'}
      onMouseDown={(e: MouseEvent) => e.preventDefault()}
      onClick={(e) => {
        e.currentTarget.blur();
        onPress();
      }}
    >
      {children ?? label}
    </button>
  );
}

export function Hud({ view, send }: ScreenProps) {
  return (
    <section className="screen screen--hud" aria-label="Game HUD">
      <div className="hud-top">
        <div className="hud-chip">
          <span className="hud-chip__game">{view.game}</span>
          <span className="hud-chip__text" data-testid="hud-text">
            {view.hud}
          </span>
        </div>
        {view.online ? (
          <span className="hud-online">online</span>
        ) : (
          <HudButton
            label="Pause"
            className="hud-button--icon"
            onPress={() => send(action.simple('pause'))}
          >
            <span className="pause-icon" aria-hidden="true" />
          </HudButton>
        )}
      </div>
      {view.commands.length > 0 ? (
        <div className="hud-commands" role="group" aria-label="Game actions">
          {view.commands.map((name) => (
            <HudButton
              key={name}
              label={commandLabel(name)}
              onPress={() => send(action.command(name))}
            />
          ))}
        </div>
      ) : null}
    </section>
  );
}
