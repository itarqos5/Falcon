import { useCallback, useEffect, useRef, useState } from 'react';
import type { ReactNode } from 'react';
import { call } from '../bridge';
import { buyUrl, dashedUuid, formatBytes, profileUrl } from '../format';
import { Check, Copy, Cpu, Download, External, Folder, Play, Refresh, ShieldAlert, Spinner, Terminal } from '../icons';
import { falconLogo } from '../logo';
import type { LauncherState } from '../types';
import { Avatar } from './Avatar';
import { ErrorCard } from './ErrorCard';

type Tab = 'overview' | 'logs';

function Notice({ title, actions, children }: { title: string; actions: ReactNode; children: ReactNode }) {
  return (
    <section className="notice card">
      <span className="notice-icon">
        <ShieldAlert size={22} />
      </span>
      <h2>{title}</h2>
      <p>{children}</p>
      <div className="btn-row">{actions}</div>
    </section>
  );
}

function CopyButton({ text, label }: { text: string; label: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <button
      className="btn small"
      onClick={() => {
        if (navigator.clipboard?.writeText) void navigator.clipboard.writeText(text).catch(() => undefined);
        setCopied(true);
        window.setTimeout(() => setCopied(false), 1600);
      }}
    >
      {copied ? <Check size={14} /> : <Copy size={14} />} {copied ? 'Copied' : label}
    </button>
  );
}

function InstallCard({ state }: { state: LauncherState }) {
  const { game } = state;
  const known = game.total > 0;
  const percent = known ? Math.min(100, Math.round((game.done / game.total) * 100)) : 0;
  // Downloads count bytes, unpacking counts files.
  const detail = !known ? '' : game.phase === 'extract' ? `${game.done} of ${game.total} files` : `${formatBytes(game.done)} of ${formatBytes(game.total)}`;
  return (
    <section className="card install" role="status">
      <div className="install-head">
        <span className="install-label">
          <Spinner size={15} /> {game.label || 'Working…'}
        </span>
        <span className="install-pct">{known ? `${percent}%` : ''}</span>
      </div>
      <div className="bar">
        <div className={`bar-fill ${known ? '' : 'indeterminate'}`} style={known ? { width: `${percent}%` } : undefined} />
      </div>
      <div className="install-foot">
        <span>{detail}</span>
        <button className="btn small" onClick={() => void call('cancelLaunch')}>
          Cancel
        </button>
      </div>
    </section>
  );
}

function Overview({ state }: { state: LauncherState }) {
  const { game, account } = state;
  const working = game.status === 'working';
  const missing = game.status === 'missing';
  return (
    <div className="columns">
      <div className="stack">
        {working && <InstallCard state={state} />}
        {game.error && <ErrorCard error={game.error} onRetry={() => void call('launch')} retryLabel="Try again" />}

        <section className="card">
          <h2>Game files</h2>
          <dl className="props">
            <div>
              <dt>Status</dt>
              <dd>
                <span className={`dot ${missing ? 'warn' : 'good'}`} /> {missing ? 'Not downloaded yet' : working ? 'Being prepared' : 'Installed'}
              </dd>
            </div>
            <div>
              <dt>Minecraft version</dt>
              <dd>{game.version}</dd>
            </div>
            <div className="wide">
              <dt>Location</dt>
              <dd className="path" title={state.app.gameDir}>
                {state.app.gameDir}
              </dd>
            </div>
          </dl>
          {missing && <p className="muted">The first launch downloads the game&rsquo;s files from Mojang (about 400 MB, once) and checks every file against its published hash.</p>}
          <div className="btn-row">
            <button className="btn small" onClick={() => void call('openFolder', { which: 'game' })}>
              <Folder size={14} /> Open folder
            </button>
            <CopyButton text={state.app.gameDir} label="Copy path" />
          </div>
        </section>

        <section className="card">
          <h2>About Falcon</h2>
          <p className="muted">
            Falcon is a Minecraft: Java Edition client written from scratch in C++ and OpenGL. It reads the game&rsquo;s own assets, which this launcher downloads from Mojang for your account.
            Nothing from Minecraft is bundled with Falcon.
          </p>
        </section>
      </div>

      <div className="stack">
        {account && (
          <section className="card">
            <h2>Account</h2>
            <div className="account-row">
              <Avatar name={account.name} avatar={account.avatar} size={56} />
              <div>
                <div className="account-name">{account.name}</div>
                <div className="owns">
                  <Check size={13} /> Owns Java Edition
                </div>
              </div>
            </div>
            <dl className="props single">
              <div className="wide">
                <dt>Account ID</dt>
                <dd className="path">{dashedUuid(account.uuid)}</dd>
              </div>
            </dl>
            <div className="btn-row">
              <CopyButton text={dashedUuid(account.uuid)} label="Copy ID" />
            </div>
          </section>
        )}
      </div>
    </div>
  );
}

function Logs() {
  const [text, setText] = useState('');
  const [loaded, setLoaded] = useState(false);
  const box = useRef<HTMLPreElement>(null);
  const load = useCallback(() => {
    call<{ text: string }>('getLog')
      .then((result) => setText(result.text))
      .catch(() => setText('The log could not be read.'))
      .finally(() => setLoaded(true));
  }, []);
  useEffect(() => {
    load();
    const timer = window.setInterval(load, 4000);
    return () => window.clearInterval(timer);
  }, [load]);
  useEffect(() => {
    if (box.current) box.current.scrollTop = box.current.scrollHeight;
  }, [text]);
  return (
    <section className="card logs">
      <div className="logs-bar">
        <h2>Client log</h2>
        <div className="btn-row">
          <button className="btn small" onClick={load}>
            <Refresh size={14} /> Refresh
          </button>
          <CopyButton text={text} label="Copy" />
          <button className="btn small" onClick={() => void call('openFolder', { which: 'logs' })}>
            <Folder size={14} /> Open folder
          </button>
        </div>
      </div>
      <pre className="log-view" ref={box} tabIndex={0}>
        {text || (loaded ? 'The log is empty.' : 'Loading…')}
      </pre>
    </section>
  );
}

export function InstancePage({ state }: { state: LauncherState }) {
  const [tab, setTab] = useState<Tab>(() => (window.location.hash === '#logs' ? 'logs' : 'overview'));
  const account = state.account;
  if (!account) return null;

  if (!account.owns) {
    return (
      <div className="page centered">
        <Notice
          title="Minecraft: Java Edition not found"
          actions={
            <>
              <button className="btn brand" onClick={() => void call('openUrl', { url: buyUrl })}>
                <External size={15} /> Get Minecraft
              </button>
              <button className="btn" onClick={() => void call('recheck')}>
                Check again
              </button>
              <button className="btn text" onClick={() => void call('signOut')}>
                Use another account
              </button>
            </>
          }
        >
          Falcon starts only for accounts that own <strong>Minecraft: Java Edition</strong>. You are signed in as <strong>{account.name || 'this account'}</strong>, which does not.
        </Notice>
      </div>
    );
  }
  if (!account.hasProfile) {
    return (
      <div className="page centered">
        <Notice
          title="Choose your player name"
          actions={
            <>
              <button className="btn brand" onClick={() => void call('openUrl', { url: profileUrl })}>
                <External size={15} /> Choose a name
              </button>
              <button className="btn" onClick={() => void call('recheck')}>
                Check again
              </button>
            </>
          }
        >
          This account owns the game but has no Minecraft profile yet. Pick a player name on minecraft.net, then check again.
        </Notice>
      </div>
    );
  }

  const { game } = state;
  const working = game.status === 'working';
  const running = game.status === 'running';
  const missing = game.status === 'missing';
  const statusText = running ? 'Running' : working ? game.label || 'Working…' : missing ? 'Not installed' : 'Ready to play';
  const statusDot = running ? 'live' : working ? 'warn' : missing ? 'idle' : 'good';

  return (
    <div className="page">
      <header className="instance-header">
        <div className="instance-icon">
          <img src={falconLogo} alt="" width={72} height={72} draggable={false} />
        </div>
        <div className="instance-meta">
          <h1>Falcon</h1>
          <div className="instance-sub">
            <span>
              <Cpu size={14} /> Minecraft Java Edition {game.version}
            </span>
            <span className="sep" />
            <span>
              <span className={`dot ${statusDot}`} /> {statusText}
            </span>
          </div>
        </div>
        <div className="instance-actions">
          <button className="btn brand large" disabled={running || working} onClick={() => void call('launch')}>
            {working ? <Spinner size={18} /> : missing ? <Download size={18} /> : <Play size={18} />}
            {running ? 'Running' : working ? 'Working' : missing ? 'Install & play' : 'Play'}
          </button>
          <button className="btn icon large" aria-label="Open the game files folder" title="Open the game files folder" onClick={() => void call('openFolder', { which: 'game' })}>
            <Folder size={18} />
          </button>
        </div>
      </header>

      <div className="tabs" role="tablist">
        <button className={`tab ${tab === 'overview' ? 'active' : ''}`} role="tab" aria-selected={tab === 'overview'} onClick={() => setTab('overview')}>
          Overview
        </button>
        <button className={`tab ${tab === 'logs' ? 'active' : ''}`} role="tab" aria-selected={tab === 'logs'} onClick={() => setTab('logs')}>
          <Terminal size={14} /> Logs
        </button>
      </div>

      {tab === 'overview' ? <Overview state={state} /> : <Logs />}
    </div>
  );
}
