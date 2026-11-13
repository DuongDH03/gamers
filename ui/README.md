# Scraps UI (React)

Menus and HUD for the Scraps WebView2 build (UI layout A,
`docs/decisions/2026-11-14-ui-layout.md`). It is a normal web app: Vite,
React 19 and TypeScript (strict). The `webview2` backend loads the built
`ui/dist` into a WebView2 window and draws the world into the page's
`<canvas id="world">`, under this UI.

Needs Node 20.19 or newer (npm 10).

```bash
cd ui
npm ci            # install the exact versions in package-lock.json
npm run dev       # dev server with hot reload, against a pretend game
npm test          # Vitest + Testing Library (jsdom)
npm run typecheck # tsc, strict, no emit
npm run build     # typecheck, then write ui/dist
npm run preview   # serve ui/dist to check the build
```

In a normal browser there is no `window.chrome.webview`, so the page uses a
**mock bridge** (`src/mockBridge.ts`) that plays the game: coins go up while
playing, pause, save and load with notices, `has_save` after a save, and
volumes. A placeholder world is drawn behind the menus. Add `?online=1` to the
URL to see the online variant (no pause, save or load).

## Screens

The native app chooses the screen; the UI only shows `view.screen`.

| Screen | What it shows | Esc |
|---|---|---|
| `main` | game name, Play, Continue (needs `has_save`), Settings, Quit | nothing |
| `playing` | HUD (`hud`), a pause button, one button per entry in `commands` | sends `pause` |
| `paused` | Resume, Save, Load (needs `has_save`), Settings, Quit | sends `resume` |
| `settings` | music and effects volume (0 to 1), Back | sends `close_settings` |

Online (`online: true`) hides Save, Load, Continue and the pause button, and
Esc does not pause. A `notice` shows for 2 s, then fades.

Menus work with the keyboard (arrows move, Enter activates, Left/Right move
a slider) and with a gamepad through the browser Gamepad API (d-pad or left
stick moves, south activates, east goes back). Focus rings are always
visible; `prefers-reduced-motion` turns animations off.

## The bridge protocol

The full definition is in `docs/specs/2026-11-14-offline-essentials-and-ui.md`,
section 6a; the types and the validator are in `src/bridge.ts`.

Native to UI, when something changed (WebView2: `PostWebMessageAsJson`):

```json
{ "type": "view", "screen": "main", "game": "sandbox", "hud": "coins 3",
  "paused": false, "has_save": true, "online": false,
  "music_volume": 0.8, "effects_volume": 1.0,
  "notice": "saved", "commands": ["drop_coin"] }
```

UI to native (WebView2: `window.chrome.webview.postMessage(object)`; read it
natively with `get_WebMessageAsJson`):

```json
{ "type": "action", "name": "play" }
{ "type": "action", "name": "set_music_volume", "value": 0.55 }
{ "type": "action", "name": "command", "command": "drop_coin" }
```

`name` is one of `play`, `continue`, `quit`, `pause`, `resume`, `save`,
`load`, `open_settings`, `close_settings`, `set_music_volume`,
`set_effects_volume`, `command`. Only the volume actions carry `value` (0 to
1, rounded to 0.01); only `command` carries `command`.

Every incoming message is checked at run time: all fields except `notice` and
`commands` are required, with exact types; volumes must be finite and within
0..1; strings are length-limited; command names match
`^[A-Za-z][A-Za-z0-9_]{0,31}$` (16 at most). Anything else is ignored and
counted in `bridge.stats.rejected`. Text is always rendered as text.

## Layout of the code

| File | What |
|---|---|
| `index.html` | the `#world` canvas and the `#ui` root |
| `src/main.tsx` | exposes `window.scrapsCanvas`, picks the bridge, mounts React |
| `src/bridge.ts` | message types, validation, the WebView2 transport |
| `src/mockBridge.ts`, `src/mockWorld.ts` | the pretend game and world for the browser |
| `src/App.tsx` | screen switch, Esc, notices |
| `src/screens/*` | main menu, HUD, pause menu, settings |
| `src/nav.ts` | arrow-key and gamepad focus in menus |
| `src/styles.css` | the look (one stylesheet, system fonts only) |
| `src/test/*` | Vitest tests |

The build is one classic script (IIFE, loaded with `defer`) and one
stylesheet with relative paths (`base: './'`), so `dist/index.html` works from
a WebView2 virtual host and from a plain `file://` URL.
