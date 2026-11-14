// Posts commits of this repository to a Discord channel through a webhook. Run by .github/workflows/discord.yml; it needs only Node (>= 18) and git.
//
//   MODE=push     the commits of one push (FROM..TO) as a single message with a link to the comparison;
//   MODE=history  every commit reachable from TO (up to and including it), oldest first, in as many messages as it takes.
//
// The webhook URL is the secret DISCORD_WEBHOOK_URL; with none set the script says so and succeeds, so a push never fails because Discord is not set up yet.
import { execFileSync } from 'node:child_process';

const env = process.env;
const webhook = (env.DISCORD_WEBHOOK_URL || '').trim();
if (!webhook) {
  console.log('DISCORD_WEBHOOK_URL is not set: nothing is posted.');
  process.exit(0);
}
const mode = env.MODE === 'history' ? 'history' : 'push';
const repo = env.REPO || 'owner/repo';
const server = env.SERVER_URL || 'https://github.com';
const actor = env.ACTOR || repo.split('/')[0];
const branch = env.REF_NAME || 'main';
const to = env.TO || 'HEAD';
const from = env.FROM || '';
const none = /^0+$/;

const git = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 1 << 26 });
const SEP = '\u001f';
const END = '\u001e';
const format = `--format=%H${SEP}%an${SEP}%s${END}`;

function commitsOf() {
  let out;
  if (mode === 'history') {
    out = git('log', '--reverse', format, to);
  } else if (from && !none.test(from)) {
    try {
      out = git('log', '--reverse', format, `${from}..${to}`);
    } catch {
      out = git('log', '--reverse', '-n', '10', format, to);  // (a forced push: the old tip is gone)
    }
  } else {
    out = git('log', '--reverse', '-n', '10', format, to);  // (a new branch)
  }
  return out
    .split(END)
    .map((record) => record.trim())
    .filter(Boolean)
    .map((record) => {
      const [sha, author, subject] = record.split(SEP);
      return { sha, author, subject };
    });
}

// Discord's markdown: keep a commit message from turning into formatting or a ping.
const clean = (text, max) => {
  const flat = text.replace(/[\r\n]+/g, ' ').replace(/([\\`*_~|>\[\]()])/g, '\\$1').replace(/@/g, '@\u200b');
  return flat.length > max ? `${flat.slice(0, max - 1)}\u2026` : flat;
};
const line = (c) => `[\`${c.sha.slice(0, 7)}\`](${server}/${repo}/commit/${c.sha}) ${clean(c.subject, 110)} \u2014 ${clean(c.author, 40)}`;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function post(body) {
  for (let attempt = 0; attempt < 6; attempt++) {
    const response = await fetch(`${webhook}?wait=false`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
    if (response.status === 429) {  // rate limited: wait as long as Discord says
      const info = await response.json().catch(() => ({}));
      await sleep(Math.ceil((info.retry_after ?? 1) * 1000) + 250);
      continue;
    }
    if (!response.ok) throw new Error(`Discord answered ${response.status}: ${(await response.text()).slice(0, 200)}`);
    return;
  }
  throw new Error('Discord kept rate limiting the webhook');
}

const commits = commitsOf();
if (commits.length === 0) {
  console.log('No commits to post.');
  process.exit(0);
}

// As many embeds as the 4096 characters of a description allow (kept well under, with room for the title).
const parts = [];
let current = [];
let size = 0;
for (const commit of commits) {
  const text = line(commit);
  if (size + text.length + 1 > 3500 && current.length > 0) {
    parts.push(current);
    current = [];
    size = 0;
  }
  current.push(text);
  size += text.length + 1;
}
parts.push(current);

const compare = mode === 'push' && from && !none.test(from) ? `${server}/${repo}/compare/${from.slice(0, 12)}...${to.slice(0, 12)}` : `${server}/${repo}/commits/${branch}`;
for (let i = 0; i < parts.length; i++) {
  const n = parts[i].length;
  const title =
    mode === 'history'
      ? `Commits on ${branch}${parts.length > 1 ? ` (part ${i + 1} of ${parts.length})` : ''}`
      : `${commits.length} new commit${commits.length === 1 ? '' : 's'} on ${branch}${parts.length > 1 ? ` (${i + 1}/${parts.length})` : ''}`;
  await post({
    username: 'Falcon',
    avatar_url: `${server}/${actor}.png`,
    allowed_mentions: { parse: [] },
    embeds: [
      {
        author: { name: mode === 'history' ? repo : actor, icon_url: `${server}/${actor}.png`, url: `${server}/${mode === 'history' ? repo : actor}` },
        title,
        url: compare,
        description: parts[i].join('\n'),
        color: mode === 'history' ? 0x5865f2 : 0x2ea043,
        footer: { text: mode === 'history' ? `${commits.length} commits so far \u00b7 ${repo}` : repo },
        timestamp: new Date().toISOString(),
      },
    ],
  });
  console.log(`Posted ${n} commit${n === 1 ? '' : 's'} (${i + 1}/${parts.length}).`);
  if (i + 1 < parts.length) await sleep(1200);
}
