const { ipcRenderer } = require('electron');

window.launcher = {
  minimize: () => ipcRenderer.send('window-minimize'),
  maximize: () => ipcRenderer.send('window-maximize'),
  close: () => ipcRenderer.send('window-close'),
  launch: (cb) => {
    ipcRenderer.send('launch-minecraft');
    ipcRenderer.once('launch-success', () => cb(null));
    ipcRenderer.once('launch-error', (_, msg) => cb(msg));
  },
  toggleOverlay: () => ipcRenderer.send('toggle-overlay'),
  openDiscord: () => ipcRenderer.send('open-discord'),
};
