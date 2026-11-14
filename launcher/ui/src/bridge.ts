import type { LauncherState } from './types';

// The way to the native side. In the launcher the page is shown by WebView2: window.chrome.webview.postMessage(string) sends a JSON message
// {id, method, params} to C++, which answers {id, ok, result | error}, and sends {event: "state", state} whenever its state changes.
// Opened in an ordinary browser (npm run dev) there is no host: devHost.ts plays a fake one so the pages can be worked on (development only).

interface WebViewHost {
  postMessage(message: string): void;
  addEventListener(type: 'message', listener: (event: MessageEvent) => void): void;
}
declare global {
  interface Window {
    chrome?: { webview?: WebViewHost };
  }
}

type Listener = () => void;
const listeners = new Set<Listener>();
let current: LauncherState | null = null;
const pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void }>();
let nextId = 1;
let send: (message: string) => void = () => undefined;

export function setState(state: LauncherState): void {
  current = state;
  listeners.forEach((listener) => listener());
}

function receive(raw: unknown): void {
  let message: { event?: string; state?: LauncherState; id?: number; ok?: boolean; result?: unknown; error?: { message?: string } };
  try {
    message = typeof raw === 'string' ? JSON.parse(raw) : (raw as typeof message);
  } catch {
    return;
  }
  if (message.event === 'state' && message.state) {
    setState(message.state);
  } else if (typeof message.id === 'number') {
    const waiting = pending.get(message.id);
    if (!waiting) return;
    pending.delete(message.id);
    if (message.ok) waiting.resolve(message.result);
    else waiting.reject(new Error(message.error?.message ?? 'The launcher could not do that.'));
  }
}

/** Sends a message to the launcher and resolves with its answer. */
export function call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
    send(JSON.stringify({ id, method, params }));
  });
}

export const store = {
  subscribe(listener: Listener): () => void {
    listeners.add(listener);
    return () => listeners.delete(listener);
  },
  getSnapshot(): LauncherState | null {
    return current;
  },
};

/** Connects to the native side (or the fake one) and tells it the page is up. */
export async function connect(): Promise<void> {
  const host = window.chrome?.webview;
  if (host) {
    send = (message) => host.postMessage(message);
    host.addEventListener('message', (event) => receive(event.data));
  } else {
    // (Only in development: the production build has no fake host in it.)
    if (!import.meta.env.DEV) return;
    const { createDevHost } = await import('./devHost');
    const dev = createDevHost(receive);
    send = dev.send;
  }
  void call('uiReady');
}
