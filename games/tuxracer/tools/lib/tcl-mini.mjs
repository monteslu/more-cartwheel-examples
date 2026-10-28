// The slice of Tcl that Tux Racer's data files use: set, proc, $var
// substitution, "quoted" and {braced} words, ;# comments, line continuation.
// No expr, no control flow. Every unknown command goes to `call(name, args)`.
export class TclMini {
  constructor(call) { this.vars = new Map(); this.procs = new Map(); this.call = call; }

  run(src) {
    for (const cmd of splitCommands(src)) {
      const words = this.words(cmd);
      if (!words.length) continue;
      const [name, ...args] = words;
      if (name === 'set') { this.vars.set(args[0], args[1] ?? ''); continue; }
      if (name === 'proc') { this.procs.set(args[0], { params: args[1].trim().split(/\s+/).filter(Boolean), body: args[2] }); continue; }
      if (name === 'global' || name === 'source' || name === 'cd' || name === 'puts') continue;
      if (this.procs.has(name)) {
        const p = this.procs.get(name);
        const saved = new Map(this.vars);
        p.params.forEach((n, i) => this.vars.set(n, args[i] ?? ''));
        this.run(p.body);
        // Tux Racer's procs set only locals; restore the caller's scope
        for (const n of p.params) saved.has(n) ? this.vars.set(n, saved.get(n)) : this.vars.delete(n);
        continue;
      }
      this.call(name, args);
    }
  }

  // Split one command into words with substitution.
  words(cmd) {
    const out = [];
    let i = 0;
    const n = cmd.length;
    while (i < n) {
      while (i < n && /\s/.test(cmd[i])) i++;
      if (i >= n) break;
      if (cmd[i] === '{') {
        let depth = 0, j = i;
        for (; j < n; j++) { if (cmd[j] === '{') depth++; else if (cmd[j] === '}' && --depth === 0) break; }
        out.push(cmd.slice(i + 1, j)); i = j + 1;
      } else if (cmd[i] === '"') {
        let j = i + 1;
        while (j < n && cmd[j] !== '"') { if (cmd[j] === '\\') j++; j++; }
        out.push(this.subst(cmd.slice(i + 1, j))); i = j + 1;
      } else {
        let j = i;
        while (j < n && !/\s/.test(cmd[j])) j++;
        out.push(this.subst(cmd.slice(i, j))); i = j;
      }
    }
    return out;
  }

  subst(s) {
    return s.replace(/\\(.)/g, '$1').replace(/\$([A-Za-z0-9_]+)/g, (m, v) => {
      if (!this.vars.has(v)) throw new Error(`tcl: unset variable $${v}`);
      return this.vars.get(v);
    });
  }
}

// Commands end at a newline or ';' outside braces / quotes; '#' at a command
// start (also after ';') begins a comment; a trailing '\' joins lines.
function splitCommands(src) {
  const cmds = [];
  let cur = '', depth = 0, quote = false, i = 0;
  const n = src.length;
  const atStart = () => cur.trim() === '';
  while (i < n) {
    const c = src[i];
    if (quote) { cur += c; if (c === '\\') { cur += src[i + 1] ?? ''; i++; } else if (c === '"') quote = false; i++; continue; }
    if (depth === 0 && c === '#' && atStart()) { while (i < n && src[i] !== '\n') i++; continue; }
    if (c === '\\' && src[i + 1] === '\n') { cur += ' '; i += 2; continue; }
    if (c === '{') depth++;
    else if (c === '}') depth--;
    else if (c === '"' && depth === 0) quote = true;
    if (depth === 0 && (c === '\n' || c === ';')) { if (cur.trim()) cmds.push(cur); cur = ''; i++; continue; }
    cur += c; i++;
  }
  if (cur.trim()) cmds.push(cur);
  return cmds;
}
