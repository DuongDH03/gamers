import { createWebViewBridge, findWebView, type Bridge } from './bridge';
import { createMockBridge } from './mockBridge';

/**
 * WebView2 if the page runs inside it, otherwise the mock game.
 * In the browser, `?online=1` makes the mock an online game.
 */
export function connectBridge(win: Window = window): Bridge {
  const webview = findWebView(win);
  if (webview) return createWebViewBridge(webview);
  const params = new URLSearchParams(win.location.search);
  return createMockBridge({ online: params.get('online') === '1' });
}
