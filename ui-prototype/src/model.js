export const STORAGE_KEY = 'moonlight-lite-ui-prototype-v1';

export const initialHosts = [
  { id: 'work', name: '工作電腦', address: '100.64.0.10', addresses: ['100.64.0.10', '192.168.1.10'], online: true, paired: true },
  { id: 'game', name: '遊戲主機', address: '192.168.1.20', addresses: ['192.168.1.20'], online: true, paired: false },
  { id: 'living', name: '客廳電腦', address: '192.168.1.30', addresses: ['192.168.1.30'], online: false, paired: true },
];

export const applications = [
  { id: 'desktop', name: '桌面', description: '使用主機的完整桌面', icon: 'monitor' },
  { id: 'steam', name: 'Steam Big Picture', description: '以手把瀏覽遊戲收藏', icon: 'gamepad' },
  { id: 'playnite', name: 'Playnite', description: '開啟遊戲啟動器', icon: 'library' },
];

export const defaults = {
  resolution: '1920x1080', customWidth: 1920, customHeight: 1080, fps: '60', customFps: 60,
  codec: 'auto', bitrate: 20, abr: false, fec: 'host', fecPercent: 20,
  streamWindow: 'fullscreen', vsync: true, pacing: false, hdr: false, hdrFormat: 'pq', hdrPeak: 1000, hdrWhite: 200,
  enhancement: false, resolutionScale: false, scaleRatio: 100, stretch: false,
  audio: 'stereo', muteHost: true, muteBackground: false,
  desktopMouse: false, localCursor: false, swapMouse: false, swapAlt: false, reverseScroll: false,
  capture: 'fullscreen', touchpad: false, precisionTouchpad: false,
  swapFace: false, deadzone: 0, forceGamepad: false, gamepadMouse: true,
  backgroundGamepad: false, quitCombo: 'default', haptics: 'physical',
  monitorMode: 'follow', monitor: 'auto', optimize: true, quitHost: false,
  decoder: 'auto', yuv444: false, discovery: false, stats: false, overlay: 'button',
  uiWindow: 'window', rememberWindow: true, connectionWarnings: true,
  configurationWarnings: true, keepAwake: true,
};

export const categories = [
  { id: 'stream', name: '影音串流', icon: 'monitor' },
  { id: 'input', name: '鍵鼠手把', icon: 'gamepad' },
  { id: 'host', name: '主機螢幕', icon: 'screens' },
  { id: 'advanced', name: '進階與其他', icon: 'sliders' },
];

export const monitorModes = [
  ['follow', '依主機設定', '使用 Sunshine 主機設定，不另行覆寫。'],
  ['none', '維持目前排列', '保留主機目前的螢幕排列，不要求調整顯示器。'],
  ['active', '啟用串流螢幕', '啟用所選螢幕來進行串流。'],
  ['primary', '設為主螢幕', '啟用所選螢幕，並設為主螢幕。'],
  ['secondary', '設為次要螢幕', '保留目前主螢幕，將所選螢幕作為次要螢幕。'],
  ['only', '僅使用串流螢幕', '只啟用所選螢幕，停用其他螢幕。'],
];

export const optionalFeatures = [
  { id: 'stretch', name: 'AR／XR 的 SBS 畫面延展', impact: '移除後無法將 Half-SBS 3D 畫面延展至僅支援 Full-SBS 的裝置；一般 2D 串流不受影響。', source: 'DisplaySettingsPage.qml' },
  { id: 'osk', name: '內建手把螢幕鍵盤', impact: '移除後輸入 IP 等文字需使用實體鍵盤或系統螢幕鍵盤；一般手把導航與輸入傳送仍保留。', source: 'GamepadKeyboard.qml' },
  { id: 'appOptions', name: '遊戲隱藏與個別直啟偏好', impact: '移除後保留完整應用程式清單、開始與恢復串流，但不再提供隱藏遊戲與個別直啟偏好。', source: 'AppView.qml' },
  { id: 'languages', name: '繁中／英文以外的介面語言', impact: '移除後只提供繁中／英文介面；主機名稱、遊戲名稱與剪貼簿文字不受限制。', source: 'LegacySettingsPage.qml' },
];

export const scopeGroups = [
  { title: '已確認保留', status: 'keep', items: ['手動新增區網／Tailscale 位址、配對與主機管理', '應用程式清單、開始／恢復／結束串流', '解析度、FPS、編碼、位元率、HDR、ABR／FEC', '一般鍵鼠與手把、文字及圖片剪貼簿、多螢幕', '效能統計與相容性設定'] },
  { title: '已確認移除／停用', status: 'remove', items: ['USB/IP 原生裝置分享（一般輸入仍保留）', '檔案傳送與檔案映射', '線上壁紙／公告、Discord、自動更新', 'STUN 與外部連線診斷、公網專用解析度／FPS 覆寫', '麥克風這次停用，保留未來評估'] },
  { title: '介面縮減提案', status: 'simplify', items: ['設定由 9 類合併為 4 類，少用選項收進進階區', '多螢幕圖解卡片改為下拉選單與文字說明', '連線過場改成清楚的狀態提示', '版本與授權集中到「關於」', 'GPUI 第一版先使用標準視窗標題列'] },
];

export function loadState(storage = globalThis.localStorage) {
  try {
    const state = JSON.parse(storage.getItem(STORAGE_KEY));
    if (!state || state.version !== 1) return {};
    const hosts = Array.isArray(state.hosts) ? state.hosts.filter(h => h && typeof h.id === 'string' && typeof h.name === 'string' && typeof h.address === 'string' && Array.isArray(h.addresses) && h.addresses.every(a => typeof a === 'string') && typeof h.online === 'boolean' && typeof h.paired === 'boolean') : undefined;
    const settings = {};
    for (const [key, initial] of Object.entries(defaults)) {
      if (typeof state.settings?.[key] === typeof initial) settings[key] = state.settings[key];
    }
    const decisions = {};
    for (const feature of optionalFeatures) {
      const value = state.decisions?.[feature.id];
      if (['pending', 'keep', 'remove'].includes(value)) decisions[feature.id] = value;
    }
    const appPreferences = {};
    for (const host of hosts || []) {
      if (!state.appPreferences?.[host.id]) continue;
      appPreferences[host.id] = {};
      for (const app of applications) {
        const preference = state.appPreferences[host.id][app.id];
        if (preference) appPreferences[host.id][app.id] = { hidden: preference.hidden === true, direct: preference.direct === true };
      }
    }
    return { hosts, settings, decisions, appPreferences, notes: typeof state.notes === 'string' ? state.notes : '' };
  } catch { return {}; }
}

export function validateAddress(value) {
  const address = value.trim();
  if (!address || /[\s/?#\\]/.test(address)) return false;
  if (address.startsWith('[')) {
    try { return new URL(`http://${address}`).hostname.startsWith('['); } catch { return false; }
  }
  if ((address.match(/:/g) || []).length > 1) {
    try { return new URL(`http://[${address}]`).hostname.startsWith('['); } catch { return false; }
  }
  const [host, port, extra] = address.split(':');
  if (extra !== undefined || (port !== undefined && (!/^\d+$/.test(port) || +port < 1 || +port > 65535))) return false;
  if (/^[\d.]+$/.test(host)) return host.split('.').length === 4 && host.split('.').every(p => /^\d{1,3}$/.test(p) && +p <= 255);
  return host.length <= 253 && host.split('.').every(p => /^[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$/.test(p));
}

export function recommendedBitrate(settings) {
  const [width, height] = settings.resolution === 'custom' ? [settings.customWidth, settings.customHeight] : settings.resolution.split('x').map(Number);
  const fps = settings.fps === 'custom' ? settings.customFps : Number(settings.fps);
  const pixels = width * height;
  // Same resolution breakpoints as StreamingPreferences::getDefaultBitrate().
  const table = [[640 * 360, 1], [854 * 480, 2], [1280 * 720, 5], [1920 * 1080, 10], [2560 * 1440, 20], [3840 * 2160, 40]];
  let base = table.at(-1)[1];
  for (let i = 0; i < table.length; i++) {
    if (pixels <= table[i][0]) {
      base = i === 0 ? table[i][1] : table[i - 1][1] + (pixels - table[i - 1][0]) / (table[i][0] - table[i - 1][0]) * (table[i][1] - table[i - 1][1]);
      break;
    }
  }
  return Math.min(150, Math.max(1, Math.round(base * (fps <= 60 ? fps / 30 : Math.sqrt(fps / 60) * 2) * (settings.yuv444 ? 2 : 1))));
}

export function validateSettings(settings) {
  if (settings.resolution === 'custom' && (![settings.customWidth, settings.customHeight].every(n => Number.isInteger(n) && n >= 256 && n <= 8192))) return '自訂解析度的寬與高須介於 256～8192。';
  if (settings.fps === 'custom' && (!Number.isInteger(settings.customFps) || settings.customFps < 1 || settings.customFps > 512)) return '自訂 FPS 須介於 1～512。';
  if (!Number.isFinite(settings.bitrate) || settings.bitrate < 1 || settings.bitrate > 150) return '位元率須介於 1～150 Mbps。';
  if (settings.fec === 'fixed' && (!Number.isInteger(settings.fecPercent) || settings.fecPercent < 0 || settings.fecPercent > 100)) return '固定 FEC 百分比須介於 0～100。';
  if (settings.resolutionScale && (!Number.isInteger(settings.scaleRatio) || settings.scaleRatio < 20 || settings.scaleRatio > 100)) return '渲染比例須介於 20～100%。';
  if (settings.hdr && (!Number.isFinite(settings.hdrPeak) || settings.hdrPeak < 100 || settings.hdrPeak > 10000 || !Number.isFinite(settings.hdrWhite) || settings.hdrWhite < 50 || settings.hdrWhite > 1000)) return 'HDR 峰值須介於 100～10000 nits，SDR 白位準須介於 50～1000 nits。';
  return '';
}
