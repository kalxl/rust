// ============================================================
//  Rust Launcher — renderer/app.js
// ============================================================

const { ipcRenderer } = require('electron');
const os = require('os');

// ── Versions — fetched live from API, fallback to hardcoded ──
const VERSIONS_API = 'https://raw.githubusercontent.com/WaypointStudios/minecraft-bedrock-versions/main/minecraft-bedrock-versions.json';

const FALLBACK_VERSIONS = [
  '1.21.51','1.21.44','1.21.43','1.21.41','1.21.40',
  '1.21.31','1.21.30','1.21.23','1.21.22','1.21.21',
  '1.21.20','1.21.3','1.21.2','1.21.1','1.21.0',
  '1.20.81','1.20.80','1.20.73','1.20.72','1.20.71',
  '1.20.62','1.20.61','1.20.60','1.20.51','1.20.50',
  '1.20.41','1.20.40','1.20.32','1.20.31','1.20.30',
  '1.20.15','1.20.12','1.20.10','1.20.1','1.20.0',
];

let VERSIONS = [];
let selectedVersion = { name: FALLBACK_VERSIONS[0] };
let installing = false;

const versionPanel    = document.getElementById('version-panel');
const versionList     = document.getElementById('version-list');
const versionTag      = document.getElementById('launch-version-tag');
const launchBtn       = document.getElementById('btn-launch');
const versionProgress = document.getElementById('version-progress');
const vpLabel         = document.getElementById('vp-label');
const vpFill          = document.getElementById('vp-fill');
const vpPct           = document.getElementById('vp-pct');

function setProgress(step, pct) {
  versionProgress.classList.add('visible');
  const labels = { fetch: 'Fetching URL…', download: 'Downloading…', install: 'Installing…' };
  vpLabel.textContent = labels[step] || step;
  vpFill.style.width  = pct + '%';
  vpPct.textContent   = pct + '%';
}

function hideProgress() {
  versionProgress.classList.remove('visible');
  vpFill.style.width = '0%';
  vpPct.textContent  = '0%';
}

function buildVersionList(versions) {
  versionList.innerHTML = '';
  versions.forEach((v, i) => {
    const item = document.createElement('div');
    item.className = 'version-item' + (v.name === selectedVersion.name ? ' selected' : '');

    const isSelected = v.name === selectedVersion.name;
    item.innerHTML = `
      <div class="version-item-left">
        <div class="version-item-name">${v.name}${i === 0 ? '&nbsp;&nbsp;<span style="color:var(--accent);font-size:10px;font-weight:700;letter-spacing:.06em">LATEST</span>' : ''}</div>
        <div class="version-item-tag">${isSelected ? 'Installed' : 'Release'}</div>
      </div>
      <span class="version-item-check">✓</span>
    `;

    item.addEventListener('click', () => {
      if (installing) return;

      // If already selected/installed just close
      if (v.name === selectedVersion.name) {
        versionPanel.classList.remove('visible');
        return;
      }

      // Start install
      installing = true;
      setProgress('fetch', 0);
      // Disable all items while installing
      document.querySelectorAll('.version-item').forEach(el => el.style.opacity = '0.4');
      item.style.opacity = '1';

      ipcRenderer.send('install-version', v.name);
    });

    versionList.appendChild(item);
  });

  selectedVersion = versions[0];
  versionTag.textContent = selectedVersion.name;
}

// Install progress events
ipcRenderer.on('install-version-progress', (_, { step, pct }) => {
  setProgress(step, pct);
});

ipcRenderer.on('install-version-done', (_, version) => {
  installing = false;
  hideProgress();
  selectedVersion = { name: version };
  versionTag.textContent = version;
  // Rebuild list to update "Installed" tag
  buildVersionList(VERSIONS);
  versionPanel.classList.remove('visible');
  launchHint.textContent = `Switched to ${version}`;
  setTimeout(() => launchHint.textContent = '', 3000);
});

ipcRenderer.on('install-version-error', (_, msg) => {
  installing = false;
  hideProgress();
  document.querySelectorAll('.version-item').forEach(el => el.style.opacity = '');
  launchHint.textContent = `Install failed: ${msg}`;
  launchHint.style.color = 'var(--accent)';
  setTimeout(() => {
    launchHint.textContent = '';
    launchHint.style.color = '';
  }, 5000);
});

async function loadVersions() {
  try {
    const res  = await fetch(VERSIONS_API);
    const data = await res.json();
    VERSIONS = data.versions.map(v => ({ name: v.value ?? v.label ?? v }));
    buildVersionList(VERSIONS);
  } catch (_) {
    VERSIONS = FALLBACK_VERSIONS.map(n => ({ name: n }));
    buildVersionList(VERSIONS);
  }
}

loadVersions();

// ── Titlebar ───────────────────────────────────────────────
document.getElementById('btn-min').addEventListener('click', () => ipcRenderer.send('window-minimize'));
document.getElementById('btn-close').addEventListener('click', () => ipcRenderer.send('window-close'));

// ── Time-based greeting ────────────────────────────────────
function setGreeting() {
  const h = new Date().getHours();
  const el = document.getElementById('greeting');
  if (h < 12)      el.textContent = 'Good Morning!';
  else if (h < 17) el.textContent = 'Good Afternoon!';
  else             el.textContent = 'Good Evening!';
}
setGreeting();

document.getElementById('btn-version-close').addEventListener('click', () => {
  versionPanel.classList.remove('visible');
});

versionPanel.addEventListener('click', (e) => {
  if (e.target === versionPanel) versionPanel.classList.remove('visible');
});

// Right-click launch button = open version panel
launchBtn.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  versionPanel.classList.add('visible');
});

// ── Launch ─────────────────────────────────────────────────
const launchText = document.getElementById('launch-text');
const launchHint = document.getElementById('launch-hint');

launchBtn.addEventListener('click', () => {
  // Windows-only guard
  if (os.platform() !== 'win32') {
    launchHint.textContent = 'Minecraft Bedrock is Windows only.';
    launchHint.style.color = 'var(--accent)';
    setTimeout(() => {
      launchHint.textContent = '';
      launchHint.style.color = '';
    }, 3000);
    return;
  }

  launchText.textContent = 'Launching…';
  launchBtn.style.opacity = '0.65';
  launchBtn.style.pointerEvents = 'none';
  launchHint.textContent = `Starting Bedrock ${selectedVersion.name}…`;
  launchHint.style.color = '';

  ipcRenderer.send('launch-minecraft', selectedVersion.name);

  ipcRenderer.once('launch-success', () => {
    launchText.textContent = 'Launched ✓';
    launchHint.textContent = 'Game is running. Good luck!';
    setTimeout(() => {
      launchText.textContent = 'Launch';
      launchBtn.style.opacity = '';
      launchBtn.style.pointerEvents = '';
      launchHint.textContent = '';
    }, 4000);
  });

  ipcRenderer.once('launch-error', (_, msg) => {
    launchText.textContent = 'Launch';
    launchBtn.style.opacity = '';
    launchBtn.style.pointerEvents = '';
    launchHint.textContent = `Error: ${msg}`;
    launchHint.style.color = 'var(--accent)';
  });
});

// ── Settings panel ─────────────────────────────────────────
const settingsPanel   = document.getElementById('settings-panel');
const keybindCapture  = document.getElementById('keybind-capture');
const keybindBtn      = document.getElementById('keybind-btn');
const keybindLabel    = document.getElementById('keybind-label');

// Load saved keybind or default to R
let menuKeybind = localStorage.getItem('menuKeybind') || 'R';
keybindLabel.textContent = menuKeybind;
keybindBtn.dataset.key   = menuKeybind;

// Save all settings to localStorage on change
function saveSettings() {
  localStorage.setItem('menuKeybind',    menuKeybind);
  localStorage.setItem('discordRpc',     document.getElementById('opt-discord-rpc').checked);
  localStorage.setItem('closeTray',      document.getElementById('opt-close-tray').checked);
  localStorage.setItem('autoInject',     document.getElementById('opt-auto-inject').checked);
  // Pass keybind to main process so it can write a config file for the DLL
  ipcRenderer.send('save-config', {
    menuKeybind,
    autoInject: document.getElementById('opt-auto-inject').checked,
  });
}

// Restore saved prefs
['opt-discord-rpc','opt-close-tray','opt-auto-inject'].forEach(id => {
  const saved = localStorage.getItem(id);
  if (saved !== null) document.getElementById(id).checked = saved === 'true';
  document.getElementById(id).addEventListener('change', saveSettings);
});

document.getElementById('btn-settings-open').addEventListener('click', () => {
  settingsPanel.classList.add('visible');
});

document.getElementById('btn-settings-close').addEventListener('click', () => {
  settingsPanel.classList.remove('visible');
});

settingsPanel.addEventListener('click', (e) => {
  if (e.target === settingsPanel) settingsPanel.classList.remove('visible');
});

// ── Keybind capture ────────────────────────────────────────
keybindBtn.addEventListener('click', () => {
  keybindCapture.classList.add('visible');
  keybindBtn.classList.add('listening');
});

document.getElementById('keybind-cancel').addEventListener('click', () => {
  keybindCapture.classList.remove('visible');
  keybindBtn.classList.remove('listening');
});

document.addEventListener('keydown', (e) => {
  if (!keybindCapture.classList.contains('visible')) return;
  e.preventDefault();

  // Ignore modifier-only presses
  if (['Control','Shift','Alt','Meta'].includes(e.key)) return;

  // Format the key nicely
  let key = e.key.toUpperCase();
  if (key === ' ') key = 'SPACE';
  if (key.startsWith('ARROW')) key = key.replace('ARROW', '');

  menuKeybind = key;
  keybindLabel.textContent = key;
  keybindBtn.dataset.key   = key;

  keybindCapture.classList.remove('visible');
  keybindBtn.classList.remove('listening');
  saveSettings();
});

// Browse game path
document.getElementById('btn-browse-path')?.addEventListener('click', () => {
  ipcRenderer.send('browse-game-path');
});

ipcRenderer.on('game-path-selected', (_, p) => {
  document.getElementById('game-path-display').textContent = p;
});

// ── Inject status from main process ───────────────────────
const injectStatus = document.getElementById('inject-status');
const injectToast  = document.getElementById('inject-toast');
const injectToastIcon = document.getElementById('inject-toast-icon');
const injectToastText = document.getElementById('inject-toast-text');

let toastTimer = null;

function showToast(message, icon, type) {
  clearTimeout(toastTimer);
  injectToastIcon.textContent = icon;
  injectToastText.textContent = message;
  injectToast.className = `inject-toast ${type} visible`;
  toastTimer = setTimeout(() => {
    injectToast.classList.remove('visible');
  }, 4000);
}

ipcRenderer.on('inject-status', (_, status) => {
  if (status === 'success') {
    injectStatus.textContent = 'Injected ✓';
    injectStatus.style.color = 'var(--accent)';
    showToast('Injected successfully', '✓', 'success');
  } else if (status === 'failed') {
    injectStatus.textContent = 'Inject failed';
    showToast('Injection failed', '✗', 'error');
  } else if (status === 'not-built') {
    injectStatus.textContent = 'Build injector first';
    showToast('Injector not found — build it first', '!', 'error');
  }
});

// ── Discord ────────────────────────────────────────────────
document.getElementById('btn-discord').addEventListener('click', () => {
  ipcRenderer.send('open-discord');
});
