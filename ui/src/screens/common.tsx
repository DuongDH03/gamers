import type { ButtonHTMLAttributes, ReactNode } from 'react';
import type { ActionMessage, ViewMessage } from '../bridge';

export interface ScreenProps {
  view: ViewMessage;
  send: (message: ActionMessage) => void;
}

interface MenuButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  children: ReactNode;
  autoFocusHere?: boolean;
}

/** A menu item: reachable with arrows and the gamepad (`data-nav`). */
export function MenuButton({ children, autoFocusHere, className, ...rest }: MenuButtonProps) {
  return (
    <button
      type="button"
      data-nav=""
      data-autofocus={autoFocusHere ? '' : undefined}
      className={className ? `menu-button ${className}` : 'menu-button'}
      {...rest}
    >
      <span className="menu-button__mark" aria-hidden="true" />
      <span className="menu-button__label">{children}</span>
    </button>
  );
}

/** The key legend under a menu. */
export function KeyHints({ back }: { back?: string }) {
  return (
    <p className="key-hints" aria-hidden="true">
      <span>
        <kbd>↑</kbd>
        <kbd>↓</kbd> choose
      </span>
      <span>
        <kbd>Enter</kbd> select
      </span>
      {back ? (
        <span>
          <kbd>Esc</kbd> {back}
        </span>
      ) : null}
    </p>
  );
}
