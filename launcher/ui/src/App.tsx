import { useEffect, useState } from 'react';
import { InstancePage } from './components/InstancePage';
import { SettingsPage } from './components/SettingsPage';
import { Setup } from './components/Setup';
import { SignIn } from './components/SignIn';
import { Sidebar } from './components/Sidebar';
import type { Page } from './components/Sidebar';
import { TitleBar } from './components/TitleBar';
import { useLauncher } from './hooks';
import { falconLogo } from './logo';

export default function App() {
  const state = useLauncher();
  const [page, setPage] = useState<Page>(() => (window.location.hash === '#settings' ? 'settings' : 'home'));
  const phase = state?.phase ?? 'boot';

  // Signing out (or in again) starts from the home page.
  useEffect(() => {
    if (phase === 'signin' || phase === 'setup') setPage('home');
  }, [phase]);

  if (!state || phase === 'boot') {
    return (
      <div className="app">
        <TitleBar />
        {/* Until the first state arrives (a few milliseconds): the same dark window with the logo, so nothing flashes. */}
        <div className="boot">
          <img src={falconLogo} alt="" width={64} height={64} draggable={false} />
          <div className="boot-bar">
            <div />
          </div>
        </div>
      </div>
    );
  }

  if (phase === 'home') {
    return (
      <div className="app">
        <TitleBar />
        <div className="body">
          <Sidebar page={page} onPage={setPage} account={state.account} running={state.game.status === 'running'} />
          <div className="content">{page === 'home' ? <InstancePage state={state} /> : <SettingsPage state={state} />}</div>
        </div>
      </div>
    );
  }

  return (
    <div className="app">
      <TitleBar />
      {phase === 'setup' ? <Setup state={state} /> : <SignIn state={state} />}
    </div>
  );
}
