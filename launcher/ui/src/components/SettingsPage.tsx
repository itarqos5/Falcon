import type { ReactNode } from 'react';
import { call } from '../bridge';
import { Folder, SignOut } from '../icons';
import type { LauncherState } from '../types';
import { Avatar } from './Avatar';

function Row({ title, description, children }: { title: string; description?: string; children: ReactNode }) {
  return (
    <div className="setting">
      <div className="setting-text">
        <div className="setting-title">{title}</div>
        {description && <div className="setting-desc">{description}</div>}
      </div>
      <div className="setting-control">{children}</div>
    </div>
  );
}

function Switch({ checked, onChange, label }: { checked: boolean; onChange: (value: boolean) => void; label: string }) {
  return (
    <label className="switch-wrap">
      <input type="checkbox" role="switch" aria-label={label} checked={checked} onChange={(e) => onChange(e.target.checked)} />
      <span className="switch" aria-hidden="true" />
    </label>
  );
}

export function SettingsPage({ state }: { state: LauncherState }) {
  const account = state.account;
  return (
    <div className="page narrow">
      <h1 className="page-title">Settings</h1>

      <section className="card">
        <h2>General</h2>
        <Row title="Keep the launcher open while Falcon runs" description="Otherwise the launcher minimises when the game starts and comes back when you close it.">
          <Switch label="Keep the launcher open while Falcon runs" checked={state.settings.keepOpen} onChange={(value) => void call('setSetting', { key: 'keepOpen', value })} />
        </Row>
      </section>

      <section className="card">
        <h2>Files</h2>
        <Row title="Game files" description={state.app.gameDir}>
          <button className="btn small" onClick={() => void call('openFolder', { which: 'game' })}>
            <Folder size={14} /> Open
          </button>
        </Row>
        <Row title="Launcher logs" description="What the launcher did, with timings.">
          <button className="btn small" onClick={() => void call('openFolder', { which: 'logs' })}>
            <Folder size={14} /> Open
          </button>
        </Row>
      </section>

      {account && account.hasProfile && (
        <section className="card">
          <h2>Account</h2>
          <div className="setting">
            <div className="setting-who">
              <Avatar name={account.name} avatar={account.avatar} size={40} />
              <div>
                <div className="setting-title">{account.name}</div>
                <div className="setting-desc">Signed in with Microsoft</div>
              </div>
            </div>
            <div className="setting-control">
              <button className="btn small danger" onClick={() => void call('signOut')}>
                <SignOut size={14} /> Sign out
              </button>
            </div>
          </div>
        </section>
      )}

      <section className="card">
        <h2>About</h2>
        <Row title="Falcon Launcher" description={`Version ${state.app.version}${state.app.mock ? ' · demo mode' : ''}`}>
          <span className="muted">Minecraft {state.app.gameVersion}</span>
        </Row>
      </section>
    </div>
  );
}
