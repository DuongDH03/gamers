import { useEffect, useId, useState, type CSSProperties } from 'react';
import { action, clampVolume, type VolumeActionName } from '../bridge';
import { KeyHints, MenuButton, type ScreenProps } from './common';

interface VolumeProps {
  label: string;
  name: VolumeActionName;
  value: number;
  send: ScreenProps['send'];
  autoFocusHere?: boolean;
}

function VolumeSlider({ label, name, value, send, autoFocusHere }: VolumeProps) {
  const id = useId();
  // Local while dragging; follows the view when native reports a change.
  const [local, setLocal] = useState(value);
  useEffect(() => setLocal(value), [value]);
  const percent = Math.round(local * 100);

  return (
    <div className="slider" style={{ '--fill': `${percent}%` } as CSSProperties}>
      <label className="slider__label" htmlFor={id}>
        {label}
      </label>
      <output className="slider__value" htmlFor={id}>
        {percent}%
      </output>
      <input
        id={id}
        className="slider__input"
        type="range"
        min={0}
        max={1}
        step={0.05}
        value={local}
        data-nav=""
        data-autofocus={autoFocusHere ? '' : undefined}
        aria-valuetext={`${percent} percent`}
        onChange={(e) => {
          const next = clampVolume(Number(e.currentTarget.value));
          if (next === local) return;
          setLocal(next);
          send(action.volume(name, next));
        }}
      />
    </div>
  );
}

export function Settings({ view, send }: ScreenProps) {
  return (
    <section className="screen screen--overlay" data-menu="" aria-labelledby="settings-title">
      <div className="panel panel--wide">
        <header className="panel__header">
          <h2 id="settings-title" className="panel__title">
            Settings
          </h2>
          <p className="panel__sub">Sound</p>
        </header>
        <div className="settings-list">
          <VolumeSlider
            label="Music"
            name="set_music_volume"
            value={view.music_volume}
            send={send}
            autoFocusHere
          />
          <VolumeSlider
            label="Effects"
            name="set_effects_volume"
            value={view.effects_volume}
            send={send}
          />
        </div>
        <nav className="menu-list menu-list--end" aria-label="Settings">
          <MenuButton onClick={() => send(action.simple('close_settings'))}>Back</MenuButton>
        </nav>
      </div>
      <KeyHints back="back" />
    </section>
  );
}
