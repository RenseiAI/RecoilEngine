#!/usr/bin/env node

/**
 * PreToolUse hook for auto-approving safe operations in RecoilEngine.
 *
 * DECISION FLOW:
 *   1. Explicit DENY for dangerous operations (with reasons)
 *   2. ALLOW for known-safe operations
 *   3. Fall through (null/no output) for everything else → user prompted
 *
 * SAFETY MODEL:
 *   - Hook errors NEVER block operations (graceful fallthrough on any crash)
 *   - Exit code 0 always, even on errors — exit(1) would block all tool use
 *   - Empty/malformed stdin → no output → user prompted (safe default)
 *
 * INPUT:  JSON via stdin { tool_name, tool_input }
 * OUTPUT: JSON via stdout { hookSpecificOutput: { hookEventName, permissionDecision, permissionDecisionReason } }
 *         No output = fall through to user prompt
 */

const PROJECT_ROOT = '/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine';
const WORKTREE_ROOT = `${PROJECT_ROOT}/.worktrees`;

// ─── Stdin reader ────────────────────────────────────────────────────────────

let inputData = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => { inputData += chunk; });
process.stdin.on('end', () => {
  try {
    if (!inputData.trim()) {
      process.exit(0); // Empty stdin → no decision → user prompted
    }
    const input = JSON.parse(inputData);
    const decision = evaluate(input);
    if (decision) {
      process.stdout.write(JSON.stringify(decision) + '\n');
    }
  } catch (_) {
    // NEVER exit(1) — that blocks the tool call entirely.
    // Silent exit(0) with no output → user gets prompted (safe fallthrough).
  }
  process.exit(0);
});

// Handle stdin errors gracefully (broken pipe, etc.)
process.stdin.on('error', () => process.exit(0));
process.stdout.on('error', () => process.exit(0));

// ─── Main evaluator ──────────────────────────────────────────────────────────

function evaluate({ tool_name, tool_input }) {
  // ── Read-only tools: always allow ──
  if (['Read', 'Glob', 'Grep', 'WebSearch', 'WebFetch'].includes(tool_name)) {
    return allow(`${tool_name}: read-only, auto-approved`);
  }

  // ── File writes: allow within project, deny outside ──
  if (['Write', 'Edit', 'NotebookEdit'].includes(tool_name)) {
    return evaluateFileWrite(tool_name, tool_input);
  }

  // ── Bash commands: most complex evaluation ──
  if (tool_name === 'Bash') {
    return evaluateBash(tool_input?.command || '');
  }

  // ── Task / subagent spawning: always allow (internal orchestration) ──
  if (tool_name === 'Task') {
    return allow('Task: subagent spawn, auto-approved');
  }

  // ── Agent tool: always allow ──
  if (tool_name === 'Agent') {
    return allow('Agent: subagent spawn, auto-approved');
  }

  // ── Task management tools: always allow ──
  if (['TaskCreate', 'TaskUpdate', 'TaskGet', 'TaskList'].includes(tool_name)) {
    return allow(`${tool_name}: task tracking, auto-approved`);
  }

  // ── Plan mode tools ──
  if (['EnterPlanMode', 'ExitPlanMode'].includes(tool_name)) {
    return allow(`${tool_name}: planning, auto-approved`);
  }

  // ── User interaction ──
  if (tool_name === 'AskUserQuestion') {
    return allow('AskUserQuestion: user interaction, auto-approved');
  }

  // ── Skill invocation ──
  if (tool_name === 'Skill') {
    return allow('Skill: skill invocation, auto-approved');
  }

  // ── MCP tools: evaluate by provider ──
  if (tool_name.startsWith('mcp__')) {
    return evaluateMcp(tool_name, tool_input);
  }

  // ── Everything else: fall through to user prompt ──
  return null;
}

// ─── File write evaluator ────────────────────────────────────────────────────

function evaluateFileWrite(tool_name, input) {
  const path = input?.file_path || input?.notebook_path || input?.path || '';

  // Allow writes within project or worktrees
  if (path.startsWith(PROJECT_ROOT) || path.startsWith(WORKTREE_ROOT)) {
    return allow(`${tool_name}: project-scoped write, auto-approved`);
  }

  // Allow writes to user's claude config
  if (path.startsWith('/Users/') && path.includes('/.claude/')) {
    return allow(`${tool_name}: claude config write, auto-approved`);
  }

  // Deny writes to system directories
  const systemPaths = ['/etc/', '/usr/', '/System/', '/Library/', '/bin/', '/sbin/'];
  if (systemPaths.some(p => path.startsWith(p))) {
    return deny(`${tool_name}: write to system path '${path}' blocked`);
  }

  return null; // Outside project → user prompted
}

// ─── Bash command evaluator ──────────────────────────────────────────────────

function evaluateBash(rawCmd) {
  const cmd = rawCmd.trim();
  if (!cmd) return null;

  // ── Step 1: Check for explicitly dangerous patterns ──
  const dangerCheck = checkDangerous(cmd);
  if (dangerCheck) return dangerCheck;

  // ── Step 2: For piped/chained commands, evaluate each segment ──
  if (/[|&;]/.test(cmd)) {
    return evaluateCompoundCommand(cmd);
  }

  // ── Step 3: Strip env var prefixes (e.g., MTL_DEBUG_LAYER=1 ./spring ...) ──
  const baseCmd = stripEnvPrefix(cmd);

  // ── Step 4: Evaluate the base command ──
  return evaluateSingleCommand(baseCmd, cmd);
}

/**
 * Explicit deny for known-dangerous commands
 */
function checkDangerous(cmd) {
  const patterns = [
    { re: /rm\s+(-[a-z]*f[a-z]*\s+)?\/\s*$/, reason: 'rm of filesystem root blocked' },
    { re: /rm\s+(-[a-z]*f[a-z]*\s+)?~\s*$/, reason: 'rm of home directory blocked' },
    { re: /mkfs\./, reason: 'filesystem format command blocked' },
    { re: /dd\s+.*of=\/dev\//, reason: 'raw disk write blocked' },
    { re: /:(){ :\|:& };:/, reason: 'fork bomb blocked' },
    { re: />\s*\/dev\/sda/, reason: 'raw disk write blocked' },
    { re: /chmod\s+-R\s+777\s+\//, reason: 'recursive chmod 777 on root blocked' },
    { re: /curl\s.*\|\s*(bash|sh|zsh)/, reason: 'pipe-to-shell blocked — review script first' },
    { re: /wget\s.*\|\s*(bash|sh|zsh)/, reason: 'pipe-to-shell blocked — review script first' },
  ];

  for (const { re, reason } of patterns) {
    if (re.test(cmd)) return deny(`Bash: ${reason}`);
  }

  return null;
}

/**
 * For compound commands (pipes, &&, ||, ;), evaluate each segment.
 * ALL segments must be safe for the compound to be approved.
 */
function evaluateCompoundCommand(cmd) {
  const segments = cmd.split(/\s*(?:\|{1,2}|&&|;)\s*/);
  const results = [];

  for (const seg of segments) {
    const trimmed = seg.trim();
    if (!trimmed) continue;

    const base = stripEnvPrefix(trimmed);
    const result = evaluateSingleCommand(base, trimmed);

    if (!result) return null; // Any unknown segment → prompt user for whole command
    if (result.hookSpecificOutput.permissionDecision === 'deny') return result;
    results.push(result);
  }

  if (results.length === 0) return null;
  return allow('Bash: compound command, all segments auto-approved');
}

/**
 * Strip leading environment variable assignments
 */
function stripEnvPrefix(cmd) {
  return cmd.replace(/^(\s*[A-Z_][A-Z0-9_]*=[^\s]*\s+)+/, '').trim();
}

/**
 * Evaluate a single (non-compound) command
 */
function evaluateSingleCommand(baseCmd, fullCmd) {
  // ── Git: blocked operations ──
  if (/^git\s+worktree\s+(remove|prune)/.test(baseCmd)) {
    return deny('Bash(git): worktree remove/prune blocked per project rules');
  }
  if (/^git\s+reset\s+--hard/.test(baseCmd)) {
    return deny('Bash(git): reset --hard blocked — use with care');
  }
  if (/^git\s+clean\s+.*-f/.test(baseCmd)) {
    return deny('Bash(git): clean -f blocked — use with care');
  }
  if (/^git\s+push\b/.test(baseCmd) && /(--force\b|-f\b)/.test(baseCmd)) {
    return deny('Bash(git): force push blocked');
  }

  // ── Git: read-only ──
  if (/^git\s+(status|log|diff|branch|show|stash\s+list|remote|tag|describe|rev-parse|ls-files|shortlog|blame|reflog|config\s+--get|config\s+--list|name-rev|for-each-ref)/.test(baseCmd)) {
    return allow('Bash(git): read-only git command, auto-approved');
  }

  // ── Git: write operations ──
  if (/^git\s+(add|commit|checkout|switch|merge|rebase|cherry-pick|fetch|pull|stash\s+(push|pop|drop|apply)|worktree\s+add|restore)/.test(baseCmd)) {
    return allow('Bash(git): write command, auto-approved');
  }

  // ── Git push (non-force): allow ──
  if (/^git\s+push\b/.test(baseCmd)) {
    if (/(--force\b|-f\b)/.test(baseCmd)) return null;
    return allow('Bash(git): push, auto-approved');
  }

  // ── C++ build tools: cmake, make, clang++, c++ ──
  if (/^cmake\b/.test(baseCmd)) {
    return allow('Bash(cmake): build system, auto-approved');
  }
  if (/^make\b/.test(baseCmd)) {
    return allow('Bash(make): build, auto-approved');
  }
  if (/^(clang\+\+|g\+\+|gcc|clang|cc|c\+\+|\/usr\/bin\/c\+\+)\b/.test(baseCmd)) {
    return allow('Bash(compiler): C/C++ compiler, auto-approved');
  }

  // ── af-linear CLI: always allow ──
  if (/^bash\s+tools\/af-linear\.sh\b/.test(baseCmd) || /af-linear\b/.test(baseCmd)) {
    return allow('Bash(af-linear): AgentFactory Linear CLI, auto-approved');
  }

  // ── af-worker-fleet: always allow ──
  if (/^bash\s+tools\/start-worker-fleet\.sh\b/.test(baseCmd) || /af-worker-fleet\b/.test(baseCmd)) {
    return allow('Bash(af-fleet): AgentFactory worker fleet, auto-approved');
  }

  // ── Metal debug tools ──
  if (/^bash\s+tools\/metal-debug\//.test(baseCmd)) {
    return allow('Bash(metal-debug): Metal debug tool, auto-approved');
  }

  // ── Python ──
  if (/^python3?\s+/.test(baseCmd) || /^pip3?\s+/.test(baseCmd)) {
    return allow('Bash(python): python command, auto-approved');
  }

  // ── Node/TypeScript execution ──
  if (/^(node|tsx|ts-node)\s+/.test(baseCmd)) {
    return allow('Bash(node): JS/TS execution, auto-approved');
  }

  // ── Package managers (in case needed for tooling) ──
  if (/^(pnpm|npm|npx|bun|bunx)\s+/.test(baseCmd)) {
    return allow('Bash(pkg): package manager, auto-approved');
  }

  // ── GitHub CLI ──
  if (/^gh\s+/.test(baseCmd)) {
    return allow('Bash(gh): GitHub CLI, auto-approved');
  }

  // ── Claude CLI (agent spawning) ──
  if (/^claude\s+/.test(baseCmd)) {
    return allow('Bash(claude): Claude CLI, auto-approved');
  }

  // ── Homebrew (install/list only) ──
  if (/^brew\s+(install|list|info|search|outdated)\b/.test(baseCmd)) {
    return allow('Bash(brew): Homebrew, auto-approved');
  }

  // ── Binary inspection (nm, otool, lipo, file) ──
  if (/^(nm|otool|lipo|file|strings|xxd|hexdump)\b/.test(baseCmd)) {
    return allow('Bash(inspect): binary inspection, auto-approved');
  }

  // ── Read-only shell commands ──
  if (/^(ls|cat|head|tail|grep|rg|find|wc|tree|pwd|echo|printf|which|type|stat|du|df|date|whoami|hostname|uname|env|printenv|realpath|dirname|basename|md5|shasum|sha256sum|man|less|more|sysctl)\b/.test(baseCmd)) {
    return allow('Bash(shell): read-only command, auto-approved');
  }

  // ── Shell utilities for scripting ──
  if (/^(xargs|tee|timeout|sort|uniq|cut|awk|sed|tr|diff|comm|paste|join|column|jq|yq|bc|expr)\b/.test(baseCmd)) {
    return allow('Bash(util): shell utility, auto-approved');
  }

  // ── File operations within project ──
  if (/^(mkdir|cp|mv|touch|ln)\b/.test(baseCmd)) {
    if (isProjectScoped(fullCmd)) {
      return allow('Bash(fs): project-scoped file operation, auto-approved');
    }
    return null;
  }

  // ── chmod (non-recursive on project files) ──
  if (/^chmod\s+/.test(baseCmd) && isProjectScoped(fullCmd)) {
    return allow('Bash(fs): project-scoped chmod, auto-approved');
  }

  // ── rm: careful handling ──
  if (/^rm\s+/.test(baseCmd)) {
    return evaluateRm(baseCmd, fullCmd);
  }

  // ── curl/wget (read-only fetches without piping to shell) ──
  if (/^(curl|wget)\s+/.test(baseCmd) && !/\|\s*(bash|sh|zsh)/.test(fullCmd)) {
    return allow('Bash(net): HTTP fetch, auto-approved');
  }

  // ── Process inspection (non-destructive) ──
  if (/^(ps|top|htop|lsof|netstat|ss|pgrep)\b/.test(baseCmd)) {
    return allow('Bash(proc): process inspection, auto-approved');
  }

  // ── Unknown command → fall through to user prompt ──
  return null;
}

/**
 * Evaluate rm commands carefully
 */
function evaluateRm(baseCmd, fullCmd) {
  if (/rm\s+(-[a-z]*f[a-z]*\s+)?\/\s*$/.test(baseCmd)) {
    return deny('Bash(rm): removing filesystem root blocked');
  }

  // Allow non-recursive rm within project
  if (!/(-r\b|-R\b|--recursive)/.test(baseCmd) && isProjectScoped(fullCmd)) {
    return allow('Bash(rm): non-recursive project-scoped delete, auto-approved');
  }

  // Allow rm -r of build artifacts within project
  if (/(-r\b|-R\b|--recursive)/.test(baseCmd)) {
    const safeTargets = /(build-arm64|node_modules|\.turbo|\.cache|\.agent-logs|\.worktrees)\b/;
    if (safeTargets.test(fullCmd) && isProjectScoped(fullCmd)) {
      return allow('Bash(rm): recursive delete of build artifacts, auto-approved');
    }
  }

  return null;
}

/**
 * Check if a command's file arguments are within the project
 */
function isProjectScoped(cmd) {
  return cmd.includes(PROJECT_ROOT) ||
    cmd.includes(WORKTREE_ROOT) ||
    // Relative paths within the project (when CWD is project root)
    /\.\/(rts|cont|tools|build-arm64|\.claude|\.agentfactory|\.agent-logs)/.test(cmd) ||
    // Common project-relative targets
    /\b(rts\/|cont\/|tools\/|build-arm64\/|\.claude\/)\b/.test(cmd);
}

// ─── MCP tool evaluator ──────────────────────────────────────────────────────

function evaluateMcp(tool_name, tool_input) {
  // ── Linear MCP: DENY ALL — use `bash tools/af-linear.sh` CLI instead ──
  // Linear MCP bypasses our rate limiter, circuit breaker, and proxy.
  // All Linear operations must go through the CLI to stay within rate limits.
  if (tool_name.startsWith('mcp__claude_ai_Linear__')) {
    return deny('Linear MCP is disabled. Use `bash tools/af-linear.sh` CLI for all Linear operations. Example: bash tools/af-linear.sh get-issue ROM-123');
  }

  // ── Vercel MCP: read operations ──
  if (tool_name.startsWith('mcp__claude_ai_Vercel__')) {
    const op = tool_name.replace('mcp__claude_ai_Vercel__', '');
    const readOps = ['list_projects', 'get_project', 'list_deployments', 'get_deployment',
      'get_deployment_build_logs', 'get_runtime_logs', 'list_teams',
      'get_access_to_vercel_url', 'web_fetch_vercel_url', 'search_vercel_documentation',
      'check_domain_availability_and_price'];
    if (readOps.includes(op)) {
      return allow(`MCP(Vercel): read operation '${op}', auto-approved`);
    }
    return null;
  }

  // ── Hugging Face MCP: all operations ──
  if (tool_name.startsWith('mcp__claude_ai_Hugging_Face__')) {
    return allow(`MCP(HuggingFace): '${tool_name.replace('mcp__claude_ai_Hugging_Face__', '')}', auto-approved`);
  }

  // ── Mermaid MCP: all operations ──
  if (tool_name.startsWith('mcp__claude_ai_Mermaid_Chart__')) {
    return allow('MCP(Mermaid): diagram tool, auto-approved');
  }

  // Unknown MCP → fall through
  return null;
}

// ─── Decision helpers ────────────────────────────────────────────────────────

function allow(reason) {
  return {
    hookSpecificOutput: {
      hookEventName: 'PreToolUse',
      permissionDecision: 'allow',
      permissionDecisionReason: reason,
    },
  };
}

function deny(reason) {
  return {
    hookSpecificOutput: {
      hookEventName: 'PreToolUse',
      permissionDecision: 'deny',
      permissionDecisionReason: reason,
    },
  };
}
