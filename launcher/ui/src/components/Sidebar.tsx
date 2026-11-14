import { useEffect, useRef, useState } from 'react';
import type { ReactNode } from 'react';
import { call } from '../bridge';
import { dashedUuid } from '../format';
import { Check, Copy, Gear, HomeIcon, SignOut } from '../icons';
import type { Account } from '../types';
import { Avatar } from './Avatar';

export type Page = 'home' | 'settings';

function NavButton({ label, active, onClick, children }: { label: string; active: boolean; onClick: () => void; children: ReactNode }) {
  return (
    <button className={`nav-btn ${active ? 'active' : ''}`} aria-label={label} aria-current={active ? 'page' : undefined} data-tip={label} onClick={onClick}>
      {children}
    </button>
  );
}

function AccountMenu({ account }: { account: Account }) {
  const [open, setOpen] = useState(false);
  const [copied, setCopied] = useState(false);
  const root = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const close = (event: MouseEvent) => {
      if (root.current && !root.current.contains(event.target as Node)) setOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setOpen(false);
    };
    window.addEventListener('mousedown', close);
    window.addEventListener('keydown', escape);
    return () => {
      window.removeEventListener('mousedown', close);
      window.removeEventListener('keydown', escape);
    };
  }, [open]);

  return (
    <div className="account-menu" ref={root}>
      <button className={`avatar-btn ${open ? 'open' : ''}`} aria-label="Account" aria-expanded={open} data-tip={open ? undefined : account.name} onClick={() => setOpen((v) => !v)}>
        <Avatar name={account.name} avatar={account.avatar} size={36} />
      </button>
      {open && (
        <div className="popover" role="menu">
          <div className="popover-head">
            <Avatar name={account.name} avatar={account.avatar} size={40} />
            <div className="popover-who">
              <strong>{account.name}</strong>
              <span>Minecraft: Java Edition</span>
            </div>
          </div>
          <button
            className="popover-item"
            role="menuitem"
            onClick={() => {
              if (navigator.clipboard?.writeText) void navigator.clipboard.writeText(dashedUuid(account.uuid)).catch(() => undefined);
              setCopied(true);
              window.setTimeout(() => setCopied(false), 1600);
            }}
          >
            {copied ? <Check size={15} /> : <Copy size={15} />} {copied ? 'Copied' : 'Copy account ID'}
          </button>
          <button
            className="popover-item danger"
            role="menuitem"
            onClick={() => {
              setOpen(false);
              void call('signOut');
            }}
          >
            <SignOut size={15} /> Sign out
          </button>
        </div>
      )}
    </div>
  );
}

export function Sidebar({ page, onPage, account, running }: { page: Page; onPage: (page: Page) => void; account: Account | null; running: boolean }) {
  return (
    <nav className="sidebar" aria-label="Main">
      <div className="sidebar-group">
        <NavButton label="Home" active={page === 'home'} onClick={() => onPage('home')}>
          <HomeIcon size={20} />
          {running && <span className="nav-live" aria-label="Falcon is running" />}
        </NavButton>
        <NavButton label="Settings" active={page === 'settings'} onClick={() => onPage('settings')}>
          <Gear size={20} />
        </NavButton>
      </div>
      <div className="sidebar-group">{account && account.hasProfile && <AccountMenu account={account} />}</div>
    </nav>
  );
}
