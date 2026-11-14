import { useEffect, useState, useSyncExternalStore } from 'react';
import { store } from './bridge';
import type { LauncherState } from './types';

/** The launcher's state (null until the native side has sent the first one). */
export function useLauncher(): LauncherState | null {
  return useSyncExternalStore(store.subscribe, store.getSnapshot);
}

/** The current time, refreshed every `interval` milliseconds (for countdowns). */
export function useNow(interval = 1000): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), interval);
    return () => window.clearInterval(timer);
  }, [interval]);
  return now;
}
