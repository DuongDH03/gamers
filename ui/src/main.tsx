import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { connectBridge } from './connect';
import { startPlaceholderWorld } from './mockWorld';
import './styles.css';

// The world canvas is plain HTML (index.html), outside React, so React never
// replaces it. The backend finds it as `window.scrapsCanvas` (or
// `#world`); `scraps:canvas-ready` fires once it is set.
const canvas = document.getElementById('world');
if (canvas instanceof HTMLCanvasElement) {
  window.scrapsCanvas = canvas;
  window.dispatchEvent(new CustomEvent('scraps:canvas-ready', { detail: canvas }));
}

const bridge = connectBridge(window);

if (bridge.kind === 'mock') {
  if (canvas instanceof HTMLCanvasElement) startPlaceholderWorld(canvas);
} else {
  // Inside the game window: no browser context menu over the game.
  window.addEventListener('contextmenu', (e) => e.preventDefault());
}

const root = document.getElementById('ui');
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App bridge={bridge} />
    </StrictMode>,
  );
}
