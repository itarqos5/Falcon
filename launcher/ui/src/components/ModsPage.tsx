import { useCallback, useEffect, useRef, useState } from 'react';
import { call } from '../bridge';
import { formatCount } from '../format';
import { Check, Download, Folder, Search, ShieldAlert, Spinner } from '../icons';
import type { LauncherState, ModHit, ModSearchResult } from '../types';
import { ErrorCard } from './ErrorCard';

const loaders = [
  { value: '', label: 'Any loader' },
  { value: 'fabric', label: 'Fabric' },
  { value: 'neoforge', label: 'NeoForge' },
  { value: 'forge', label: 'Forge' },
  { value: 'quilt', label: 'Quilt' },
];

function ModCard({ hit, state, onInstall, failure }: { hit: ModHit; state: LauncherState; onInstall: (hit: ModHit) => void; failure?: string }) {
  const installed = state.mods.installed[hit.projectId];
  const busy = state.mods.busy[hit.projectId];
  return (
    <li className={hit.blocked ? 'mod blocked' : 'mod'}>
      {hit.icon ? <img className="mod-icon" src={hit.icon} alt="" width={64} height={64} draggable={false} /> : <span className="mod-icon placeholder" aria-hidden="true" />}
      <div className="mod-text">
        <div className="mod-head">
          <span className="mod-title">{hit.title}</span>
          {hit.author && <span className="mod-author">by {hit.author}</span>}
        </div>
        <p className="mod-desc">{hit.description}</p>
        <div className="mod-meta">
          <span>{formatCount(hit.downloads)} downloads</span>
          {hit.categories.slice(0, 4).map((category) => (
            <span className="chip" key={category}>
              {category}
            </span>
          ))}
        </div>
        {hit.blocked && <div className="mod-error">{hit.blockedReason || 'Optimization mods are not compatible with Falcon and never will be.'}</div>}
        {failure && <div className="mod-error">{failure}</div>}
      </div>
      <div className="mod-action">
        {hit.blocked ? (
          <button className="btn small" disabled title={hit.blockedReason}>
            <ShieldAlert size={14} /> Not compatible
          </button>
        ) : busy ? (
          <button className="btn small" disabled title={busy}>
            <Spinner size={14} /> Installing
          </button>
        ) : installed ? (
          <button className="btn small installed" disabled title={`${installed.file}`}>
            <Check size={14} /> Installed
          </button>
        ) : (
          <button className="btn small brand" onClick={() => onInstall(hit)}>
            <Download size={14} /> Install
          </button>
        )}
        {installed && !busy && <span className="mod-version">{installed.version}</span>}
      </div>
    </li>
  );
}

/** Said at the top of the tab, always: optimization mods are not compatible with Falcon, and the launcher does not install them. */
function OptimizationNotice({ state }: { state: LauncherState }) {
  return (
    <>
      <div className="admonition warn" role="note">
        <span className="admonition-icon">
          <ShieldAlert size={20} />
        </span>
        <div className="admonition-body">
          <div className="admonition-title">Optimization mods are not compatible with Falcon, and never will be</div>
          <p>
            Mods like <strong>Sodium</strong>, <strong>Lithium</strong>, <strong>Iris</strong> and <strong>FerriteCore</strong> replace parts of the game that Falcon has written itself: the renderer, the lighting and the
            chunk loading. They cannot work with it, so Falcon does not support them and the launcher does not install them. Mods that need one are left out too.
          </p>
        </div>
      </div>
      {state.mods.blocked.length > 0 && (
        <div className="admonition warn" role="alert">
          <span className="admonition-icon">
            <ShieldAlert size={20} />
          </span>
          <div className="admonition-body">
            <div className="admonition-title">
              {state.mods.blocked.length === 1 ? 'An optimization mod is in your mods folder' : `${state.mods.blocked.length} optimization mods are in your mods folder`}
            </div>
            <ul className="mods-blocked">
              {state.mods.blocked.map((mod) => (
                <li key={mod.file} title={mod.reason}>
                  <strong>{mod.name}</strong> <span className="path">{mod.file}</span>
                </li>
              ))}
            </ul>
            <p>Falcon will not use {state.mods.blocked.length === 1 ? 'it' : 'them'}. You can delete {state.mods.blocked.length === 1 ? 'the file' : 'the files'} from the mods folder.</p>
          </div>
        </div>
      )}
    </>
  );
}

/** The Mods tab: search Modrinth for mods of this game version and install their .jar into Falcon's mods folder. */
export function ModsPage({ state }: { state: LauncherState }) {
  const [text, setText] = useState('');
  const [loader, setLoader] = useState('');
  const [hits, setHits] = useState<ModHit[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<{ code?: string; message?: string } | null>(null);
  const [failures, setFailures] = useState<Record<string, string>>({});
  const latest = useRef(0);

  const search = useCallback((query: string, loaderValue: string, offset: number) => {
    const mine = ++latest.current;
    setLoading(true);
    setError(null);
    call<ModSearchResult>('searchMods', { query, loader: loaderValue, offset })
      .then((result) => {
        if (mine !== latest.current || result.superseded) return;
        setHits((previous) => (offset === 0 ? result.hits : [...previous, ...result.hits]));
        setTotal(result.total);
        setLoading(false);
      })
      .catch((reason: Error & { code?: string }) => {
        if (mine !== latest.current) return;
        setError({ code: 'network', message: reason.message });
        setLoading(false);
      });
  }, []);

  // The first look is the most downloaded mods; after that a search runs when the player stops typing (or changes the loader).
  useEffect(() => {
    const timer = window.setTimeout(() => search(text.trim(), loader, 0), text === '' ? 0 : 350);
    return () => window.clearTimeout(timer);
  }, [text, loader, search]);

  const install = (hit: ModHit) => {
    setFailures((previous) => {
      const rest = { ...previous };
      delete rest[hit.projectId];
      return rest;
    });
    call('installMod', { projectId: hit.projectId, title: hit.title, loader }).catch((reason: Error) => setFailures((previous) => ({ ...previous, [hit.projectId]: reason.message })));
  };

  const more = hits.length < total;
  return (
    <div className="stack">
      <OptimizationNotice state={state} />
      <section className="card mods-bar">
        <div className="mods-search">
          <label className="search">
            <Search size={15} />
            <input type="search" placeholder={`Search mods for Minecraft ${state.mods.gameVersion}`} value={text} onChange={(event) => setText(event.target.value)} spellCheck={false} aria-label="Search mods" />
          </label>
          <select className="select" value={loader} onChange={(event) => setLoader(event.target.value)} aria-label="Mod loader">
            {loaders.map((l) => (
              <option key={l.value} value={l.value}>
                {l.label}
              </option>
            ))}
          </select>
          <button className="btn small" onClick={() => void call('openFolder', { which: 'mods' })}>
            <Folder size={14} /> Open mods folder
          </button>
        </div>
        <p className="muted mods-note">
          Mods come from <strong>Modrinth</strong> and are saved as .jar files in <span className="path">{state.mods.dir}</span>. Falcon is not built on Java, so it does not run .jar mods itself yet; installing one only puts the file there.
        </p>
      </section>

      {error && <ErrorCard error={error} onRetry={() => search(text.trim(), loader, 0)} />}

      <ul className="mods">
        {hits.map((hit) => (
          <ModCard key={hit.projectId} hit={hit} state={state} onInstall={install} failure={failures[hit.projectId]} />
        ))}
      </ul>

      {loading && (
        <div className="mods-status" role="status">
          <Spinner size={16} /> {hits.length === 0 ? 'Looking on Modrinth…' : 'Loading…'}
        </div>
      )}
      {!loading && !error && hits.length === 0 && <div className="mods-status">No mods found{text ? ` for “${text}”` : ''}.</div>}
      {!loading && more && (
        <div className="btn-row mods-more">
          <button className="btn" onClick={() => search(text.trim(), loader, hits.length)}>
            Show more
          </button>
        </div>
      )}
    </div>
  );
}
