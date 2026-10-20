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
  streamWindow: 'fullscreen', vsync: true, pacing: false, hdr: false, hdrFormat: 'pq', hdrBrightnessMode: 'host', hdrPeak: 1000, hdrMin: 0.001, hdrFullFrame: 400,
  enhancement: false, resolutionScale: false, scaleRatio: 100, stretch: false,
  audio: 'stereo', muteHost: true, muteBackground: false,
  desktopMouse: false, localCursor: false, swapMouse: false, swapAlt: false, reverseScroll: false,
  capture: 'fullscreen', touchpad: false, precisionTouchpad: false,
  swapFace: false, deadzone: 0, forceGamepad: false, gamepadMouse: true,
  backgroundGamepad: false, quitCombo: 'default', haptics: 'physical',
  monitorMode: 'follow', monitor: 'auto', optimize: true, quitHost: false,
  decoder: 'auto', yuv444: false, discovery: false, stats: false, overlay: 'button',
  uiWindow: 'window', language: 'zh-TW', rememberWindow: true, connectionWarnings: true,
  configurationWarnings: true, keepAwake: true,
};

export const categories = [
  { id: 'stream', name: '串流', icon: 'monitor' },
  { id: 'display', name: '顯示', icon: 'screens' },
  { id: 'audio', name: '音效', icon: 'sliders' },
  { id: 'host', name: '主機', icon: 'screens' },
  { id: 'input', name: '鍵盤與滑鼠', icon: 'sliders' },
  { id: 'gamepad', name: '手把', icon: 'gamepad' },
  { id: 'advanced', name: '相容性與診斷', icon: 'sliders' },
  { id: 'ui', name: '介面', icon: 'monitor' },
];

export const monitorModes = [
  ['follow', '依主機設定', '使用 Sunshine 主機設定，不另行覆寫。'],
  ['none', '維持目前排列', '保留主機目前的螢幕排列，不要求調整顯示器。'],
  ['active', '啟用串流螢幕', '啟用所選螢幕來進行串流。'],
  ['primary', '設為主螢幕', '啟用所選螢幕，並設為主螢幕。'],
  ['secondary', '設為次要螢幕', '保留目前主螢幕，將所選螢幕作為次要螢幕。'],
  ['only', '僅使用串流螢幕', '只啟用所選螢幕，停用其他螢幕。'],
];

export const optionalFeatures = [];
// Accepted Windows PC scope: no SBS stretch or client controller keyboard;
// retain app hiding/direct launch and Traditional/Simplified Chinese + English.
export const confirmedDecisions = { stretch: 'remove', osk: 'remove', appOptions: 'keep', languages: 'remove' };


// Review inventory of the current Lite QML settings, not an implemented GPUI contract.
export const settingsCoverage = [
  { category: '串流', source: 'BasicSettingsPage.qml', coverage: '解析度／自訂、FPS／自訂、編碼、位元率、ABR、FEC、AI 增強、渲染比例', limitation: '解析度快捷清單暫列常用值；其他尺寸可自訂。GPU／主機能力判斷尚未接上。' },
  { category: '顯示', source: 'DisplaySettingsPage.qml、HdrBrightnessCard.qml', coverage: '視窗模式、VSync、影格節奏、HDR 格式、亮度模式、峰值／最低／全畫面亮度', limitation: '上一版錯用 SDR 白位準，現已改回原版三種亮度值。PQ 拖曳曲線與實際顯示器偵測尚未重製。' },
  { category: '音效', source: 'LegacySettingsPage.qml', coverage: '立體聲／5.1／7.1／7.1.4、主機靜音；另預覽背景靜音偏好', limitation: '不播放音效；麥克風本次停用。' },
  { category: '主機', source: 'LegacySettingsPage.qml、ScreenCombinationModeSelector.qml', coverage: '六種螢幕組合模式、螢幕選擇、最佳化遊戲設定、結束主機程式', limitation: '螢幕清單使用範例；圖解卡片改選單是呈現提案，未刪除控制模式。' },
  { category: '鍵盤與滑鼠', source: 'LegacySettingsPage.qml', coverage: '桌面滑鼠、本機游標、左右鍵／Alt-Win 交換、反向捲動、快捷鍵攔截、觸控板', limitation: '原型不攔截系統按鍵；文字／圖片剪貼簿能力保留，沒有檔案傳送。' },
  { category: '手把', source: 'LegacySettingsPage.qml', coverage: 'ABXY 交換、死區、強制連線、手把滑鼠、結束組合鍵、DualSense 觸覺；另預覽背景輸入偏好', limitation: '一般手把映射保留；不提供客戶端手把螢幕鍵盤。硬體輸入與觸覺尚未接上。' },
  { category: '相容性與診斷', source: 'LegacySettingsPage.qml', coverage: '解碼器、YUV 4:4:4、區網搜尋、統計、串流控制選單位置', limitation: 'Windows 範圍不列 macOS 專用 Renderer 選單；不代表移除 Windows D3D11 路徑。' },
  { category: '介面與關於', source: 'LegacySettingsPage.qml、AboutSettingsPage.qml', coverage: '語言選項、視窗模式／位置、警告、保持喚醒、授權', limitation: '上一版只列語言說明，現補選擇入口；實際翻譯切換尚未接上，語系確定為繁中、簡中與英文。' },
];

export const scopeGroups = [
  { title: '已確認保留', status: 'keep', items: ['手動新增區網／Tailscale 位址、配對與主機管理', '應用程式清單、開始／恢復／結束串流', '解析度、FPS、編碼、位元率、HDR、ABR／FEC', '一般鍵鼠與手把、文字及圖片剪貼簿、多螢幕', '效能統計與相容性設定', '遊戲隱藏與直啟、繁中／簡中／英文介面'] },
  { title: '已確認移除／停用', status: 'remove', items: ['USB/IP 原生裝置分享（一般輸入仍保留）', '檔案傳送與檔案映射', '線上壁紙／公告、Discord、自動更新', 'STUN 與外部連線診斷、公網專用解析度／FPS 覆寫', '麥克風這次停用，保留未來評估', 'SBS 強制拉伸、客戶端手把螢幕鍵盤及三語以外介面'] },
  { title: '介面調整提案', status: 'simplify', items: ['沿用原版裝置首頁，點選裝置後再進入配對／應用程式', '設定保留原版分類：8 類設定與獨立的「關於」', '設定項目直接展開，不再集中藏在進階摺疊區', '多螢幕圖解卡片暫以選單對照，功能不刪除', 'GPUI 第一版先使用標準視窗標題列'] },
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
    if (settings.language && !['zh-TW', 'zh-CN', 'en'].includes(settings.language)) settings.language = 'zh-TW';
    const decisions = { ...confirmedDecisions };
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
  if (settings.hdr && settings.hdrBrightnessMode === 'manual' && (!Number.isFinite(settings.hdrPeak) || settings.hdrPeak < 1 || settings.hdrPeak > 10000 || !Number.isFinite(settings.hdrMin) || settings.hdrMin < 0 || settings.hdrMin > 10 || !Number.isFinite(settings.hdrFullFrame) || settings.hdrFullFrame < 1 || settings.hdrFullFrame > 10000 || settings.hdrMin > settings.hdrFullFrame || settings.hdrFullFrame > settings.hdrPeak)) return 'HDR 亮度須符合：最低（0～10）≤ 全畫面（1～10000）≤ 峰值（1～10000 nits）。';
  return '';
}
