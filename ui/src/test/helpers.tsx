import { act, render } from '@testing-library/react';
import { App } from '../App';
import { createWebViewBridge, type ViewMessage, type WebViewLike } from '../bridge';

export function baseView(overrides: Partial<ViewMessage> = {}): ViewMessage {
  return {
    type: 'view',
    screen: 'main',
    game: 'sandbox',
    hud: 'coins 3',
    paused: false,
    has_save: false,
    online: false,
    music_volume: 0.8,
    effects_volume: 1.0,
    notice: '',
    commands: ['drop_coin'],
    ...overrides,
  };
}

/**
 * A stand-in for `window.chrome.webview`. `posted` holds what the page sent,
 * as JSON-parsed copies (what native would read with get_WebMessageAsJson);
 * `deliver` plays a PostWebMessageAsJson from native.
 */
export function fakeWebView() {
  const target = new EventTarget();
  const posted: unknown[] = [];
  const wrapped = new Map<unknown, EventListener>();
  const webview: WebViewLike = {
    postMessage(message) {
      posted.push(JSON.parse(JSON.stringify(message)));
    },
    addEventListener(type, listener) {
      const l: EventListener = (e) => listener(e as unknown as { data: unknown });
      wrapped.set(listener, l);
      target.addEventListener(type, l);
    },
    removeEventListener(type, listener) {
      const l = wrapped.get(listener);
      if (l) target.removeEventListener(type, l);
      wrapped.delete(listener);
    },
  };
  const deliver = (data: unknown) => {
    const event = new Event('message');
    Object.defineProperty(event, 'data', { value: data });
    target.dispatchEvent(event);
  };
  return { webview, posted, deliver, listenerCount: () => wrapped.size };
}

/** Renders the app over a fake WebView2 and sends it `view`. */
export function renderApp(view?: Partial<ViewMessage>) {
  const fake = fakeWebView();
  const bridge = createWebViewBridge(fake.webview);
  const result = render(<App bridge={bridge} />);
  const show = (v: Partial<ViewMessage>) => act(() => fake.deliver(baseView(v)));
  const deliverRaw = (data: unknown) => act(() => fake.deliver(data));
  if (view) show(view);
  return { ...result, ...fake, bridge, show, deliverRaw };
}
