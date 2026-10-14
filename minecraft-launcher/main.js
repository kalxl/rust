const { app, BrowserWindow, ipcMain, shell } = require('electron');
const path  = require('path');
const { execFile, spawn } = require('child_process');
const os    = require('os');
const fs    = require('fs');
const https = require('https');

// When packaged, __dirname is inside the asar — use app.getAppPath() instead
const ROOT = app.isPackaged
  ? path.dirname(app.getPath('exe'))
  : __dirname;

const RESOURCES = app.isPackaged
  ? path.join(process.resourcesPath, 'resources')
  : path.join(__dirname, 'resources');

let mainWindow;
let overlayWindow;

// ── Window creation ───────────────────────────────────────

function createMainWindow() {
  mainWindow = new BrowserWindow({
    width: 720,
    height: 460,
    minWidth: 580,
    minHeight: 380,
    title: 'Rust',
    frame: false,
    transparent: true,
    resizable: false,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      preload: path.join(__dirname, 'preload.js'),
      webSecurity: false
    },
    icon: path.join(__dirname, 'assets', 'logo.png'),
    backgroundColor: '#00000000'
  });

  // Use app.getAppPath() so the path resolves correctly both in dev and
  // when packaged inside an .asar (where __dirname is the asar root)
  mainWindow.loadFile(path.join(app.getAppPath(), 'renderer', 'index.html'));
  mainWindow.on('closed', () => { mainWindow = null; app.quit(); });
}

function createOverlayWindow() {
  overlayWindow = new BrowserWindow({
    width: 220, height: 300,
    frame: false, transparent: true,
    alwaysOnTop: true, skipTaskbar: true, resizable: false,
    webPreferences: { nodeIntegration: true, contextIsolation: false }
  });
  overlayWindow.loadFile(path.join(app.getAppPath(), 'renderer', 'overlay.html'));
}

app.whenReady().then(createMainWindow);
app.on('window-all-closed', () => app.quit());

// ── Basic IPC ─────────────────────────────────────────────

ipcMain.on('window-minimize', () => mainWindow?.minimize());
ipcMain.on('window-maximize', () => {
  mainWindow?.isMaximized() ? mainWindow.unmaximize() : mainWindow?.maximize();
});
ipcMain.on('window-close', () => app.quit());

ipcMain.on('toggle-overlay', () => {
  if (overlayWindow) { overlayWindow.close(); overlayWindow = null; }
  else createOverlayWindow();
});

ipcMain.on('open-discord', () => shell.openExternal('https://discord.gg/minecraft'));

ipcMain.on('overlay-settings', (_, s) => overlayWindow?.webContents.send('update-settings', s));

ipcMain.on('save-config', (_, config) => {
  try {
    const cfgPath = path.join(RESOURCES, 'rust_config.json');
    fs.mkdirSync(RESOURCES, { recursive: true });
    fs.writeFileSync(cfgPath, JSON.stringify(config, null, 2));
  } catch (_) {}
});

ipcMain.on('browse-game-path', async (event) => {
  const { dialog } = require('electron');
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Select Minecraft Bedrock folder',
    properties: ['openDirectory']
  });
  if (!result.canceled && result.filePaths.length > 0)
    event.reply('game-path-selected', result.filePaths[0]);
});

// ── Launch Minecraft ──────────────────────────────────────

ipcMain.on('launch-minecraft', (event, version) => {
  if (os.platform() !== 'win32') {
    event.reply('launch-error', 'Minecraft Bedrock is only available on Windows.');
    return;
  }

  const launchGame = () => new Promise((resolve, reject) => {
    shell.openExternal('minecraft://').then(resolve).catch(() => {
      execFile('explorer.exe',
        ['shell:AppsFolder\\Microsoft.MinecraftUWP_8wekyb3d8bbwe!App'],
        (err) => err ? reject(err) : resolve());
    });
  });

  launchGame().then(() => {
    event.reply('launch-success');

    const injectorPath = path.join(RESOURCES, 'RustInjector.exe');
    const dllPath      = path.join(RESOURCES, 'RustDLL.dll');

    if (!fs.existsSync(injectorPath)) {
      mainWindow?.webContents.send('inject-status', 'not-built');
      return;
    }

    const injector = spawn(injectorPath, [dllPath], { detached: true, stdio: 'pipe' });
    injector.stdout?.on('data', (data) => {
      const msg = data.toString().trim();
      if (msg.includes('INJECT_SUCCESS')) mainWindow?.webContents.send('inject-status', 'success');
      else if (msg.includes('INJECT_FAIL')) mainWindow?.webContents.send('inject-status', 'failed');
    });
    injector.unref();

  }).catch((err) => event.reply('launch-error', err.message));
});

// ── Version switcher ──────────────────────────────────────

const MC_PRODUCT_ID = '9NBLGGH2JHXJ';

// Get direct .appx download URL from store.rg-adguard.net
function getDownloadUrl(version) {
  return new Promise((resolve, reject) => {
    const body = `type=url&url=https://www.microsoft.com/store/productId/${MC_PRODUCT_ID}&ring=Retail&lang=en-US`;
    const opts  = {
      hostname: 'store.rg-adguard.net',
      path:     '/api/GetFiles',
      method:   'POST',
      headers: {
        'Content-Type':   'application/x-www-form-urlencoded',
        'Content-Length': Buffer.byteLength(body),
        'User-Agent':     'Mozilla/5.0'
      }
    };

    const req = https.request(opts, (res) => {
      let html = '';
      res.on('data', c => html += c);
      res.on('end', () => {
        // Parse all .appx / .msixbundle links from the HTML response
        const re = /href="(https:\/\/[^"]+\.(?:appx|msixbundle|msix)[^"]*)"/gi;
        const all = [];
        let m;
        while ((m = re.exec(html)) !== null) all.push(m[1]);

        // Filter to links containing our version number
        let matches = all.filter(u => u.includes(version));

        // If nothing version-specific, take any Minecraft package
        if (!matches.length) matches = all.filter(u =>
          u.toLowerCase().includes('minecraft') ||
          u.includes('8wekyb3d8bbwe'));

        // Prefer x64 over arm
        const url = matches.find(u => u.includes('x64'))
                 || matches.find(u => !u.includes('arm'))
                 || matches[0];

        if (url) resolve(url);
        else reject(new Error(`No download link found for ${version}. The proxy may be down.`));
      });
    });

    req.on('error', reject);
    req.write(body);
    req.end();
  });
}

// Download with progress
function downloadFile(url, dest, onProgress) {
  return new Promise((resolve, reject) => {
    const file = fs.createWriteStream(dest);

    const get = (u) => {
      https.get(u, { headers: { 'User-Agent': 'Mozilla/5.0' } }, (res) => {
        if (res.statusCode === 301 || res.statusCode === 302) {
          return get(res.headers.location);
        }
        if (res.statusCode !== 200) {
          return reject(new Error(`HTTP ${res.statusCode}`));
        }

        const total = parseInt(res.headers['content-length'] || '0', 10);
        let got = 0;

        res.on('data', chunk => {
          got += chunk.length;
          if (total > 0) onProgress(Math.round((got / total) * 100));
        });

        res.pipe(file);
        file.on('finish', () => file.close(resolve));
      }).on('error', reject);
    };

    get(url);
    file.on('error', reject);
  });
}

// Install via PowerShell
function installAppx(appxPath) {
  return new Promise((resolve, reject) => {
    const escaped = appxPath.replace(/'/g, "''");
    const ps = `
      Get-Process -Name "Minecraft.Windows" -ErrorAction SilentlyContinue | Stop-Process -Force
      Start-Sleep -Milliseconds 1500
      Add-AppxPackage -Path '${escaped}' -ForceApplicationShutdown
    `;
    const proc = spawn('powershell.exe',
      ['-NoProfile', '-NonInteractive', '-Command', ps],
      { stdio: 'pipe' });

    let err = '';
    proc.stderr?.on('data', d => err += d.toString());
    proc.on('close', code => {
      if (code === 0) resolve();
      else reject(new Error(`Install failed (${code}): ${err.trim()}`));
    });
  });
}

ipcMain.on('install-version', async (event, version) => {
  if (os.platform() !== 'win32') {
    event.reply('install-version-error', 'Windows only.');
    return;
  }

  const tmpPath = path.join(os.tmpdir(), `minecraft_${version}.appx`);

  try {
    event.reply('install-version-progress', { step: 'fetch', pct: 0 });
    const url = await getDownloadUrl(version);

    event.reply('install-version-progress', { step: 'download', pct: 0 });
    await downloadFile(url, tmpPath, pct =>
      event.reply('install-version-progress', { step: 'download', pct }));

    event.reply('install-version-progress', { step: 'install', pct: 99 });
    await installAppx(tmpPath);

    try { fs.unlinkSync(tmpPath); } catch (_) {}
    event.reply('install-version-done', version);

  } catch (err) {
    try { fs.unlinkSync(tmpPath); } catch (_) {}
    event.reply('install-version-error', err.message);
  }
});
