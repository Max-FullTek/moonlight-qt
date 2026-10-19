import { useState } from 'react';
import { applications, validateAddress } from './model';
import { Button, Dialog, Icon } from './components';

export function AddHostDialog({ hosts, onAdd, onClose, oskEnabled }) {
  const [name, setName] = useState('');
  const [address, setAddress] = useState('');
  const [error, setError] = useState('');
  const [keyboard, setKeyboard] = useState(false);
  const submit = e => {
    e.preventDefault();
    if (!validateAddress(address)) { setError('請輸入有效的 IP 位址或主機名稱，可附加連接埠。'); return; }
    if (hosts.some(h => h.addresses.includes(address.trim()))) { setError('這個位址已存在於主機清單。'); return; }
    onAdd({ id: crypto.randomUUID(), name: name.trim() || address.trim(), address: address.trim(), addresses: [address.trim()], online: true, paired: false });
  };
  return <Dialog title="新增主機" onClose={onClose}><form onSubmit={submit}><p className="dialog-description">輸入主機的區網或 Tailscale 位址。新增後即可預覽配對流程。</p><label className="input-label" htmlFor="host-address">IP 位址或主機名稱</label><input id="host-address" value={address} onChange={e => { setAddress(e.target.value); setError(''); }} placeholder="例如 192.168.1.10 或 100.64.0.10" autoFocus autoComplete="off" aria-invalid={!!error} aria-describedby={error ? 'address-error' : undefined}/><label className="input-label" htmlFor="host-name">顯示名稱（選填）</label><input id="host-name" value={name} onChange={e => setName(e.target.value)} placeholder="例如：書房電腦" maxLength={60}/>{error ? <p id="address-error" className="error-text" role="alert">{error}</p> : null}
      {oskEnabled ? <><button type="button" className="text-button keyboard-link" onClick={() => setKeyboard(!keyboard)}>{keyboard ? '收起螢幕鍵盤預覽' : '預覽手把螢幕鍵盤'}</button>{keyboard ? <div className="demo-keyboard" aria-label="原型螢幕鍵盤">{'1234567890.'.split('').map(key => <button type="button" key={key} onClick={() => setAddress(address + key)}>{key}</button>)}<button type="button" onClick={() => setAddress(address.slice(0, -1))}>退格</button><p>僅預覽數字輸入；完整手把導航列為 GPUI 驗證項目。</p></div> : null}</> : null}
      <div className="dialog-actions"><Button type="button" onClick={onClose}>取消</Button><Button primary type="submit">新增並配對</Button></div></form></Dialog>;
}

export function PairDialog({ host, onSuccess, onFailure, onClose }) {
  const [pin] = useState(() => String(1000 + crypto.getRandomValues(new Uint32Array(1))[0] % 9000));
  return <Dialog title="配對主機" onClose={onClose}><p className="dialog-description">將以下 PIN 輸入「{host.name}」的 Sunshine 配對頁面。</p><div className="pair-pin" aria-label={`配對 PIN ${pin.split('').join(' ')}`}>{pin}</div><p className="help-text">互動原型不會連線主機。使用下方按鈕檢查配對結果。</p><div className="dialog-actions wrap"><Button onClick={onClose}>取消</Button><Button onClick={onFailure}>模擬配對失敗</Button><Button primary onClick={onSuccess}>模擬配對成功</Button></div></Dialog>;
}

export function HostOptionsDialog({ host, onSave, onDelete, onClose }) {
  const [name, setName] = useState(host.name);
  const [address, setAddress] = useState(host.address);
  return <Dialog title="主機選項" onClose={onClose}><form onSubmit={e => { e.preventDefault(); onSave({ ...host, name: name.trim() || host.name, address }); }}><label className="input-label" htmlFor="rename-host">顯示名稱</label><input id="rename-host" value={name} onChange={e => setName(e.target.value)} maxLength={60}/><label className="input-label" htmlFor="select-address">連線位址</label><select id="select-address" value={address} onChange={e => setAddress(e.target.value)}>{host.addresses.map(a => <option key={a}>{a}</option>)}</select><dl className="host-details"><dt>狀態</dt><dd>{host.online ? '在線' : '離線'}／{host.paired ? '已配對' : '尚未配對'}</dd><dt>資料來源</dt><dd>本機範例資料</dd></dl><div className="dialog-actions split"><Button type="button" danger onClick={onDelete}>移除範例主機</Button><div className="button-row"><Button type="button" onClick={onClose}>取消</Button><Button primary type="submit">儲存</Button></div></div></form></Dialog>;
}

export function AppOptionsDialog({ host, app, preferences, onChange, onClose }) {
  const apps = app ? [app] : applications;
  return <Dialog title={app ? `${app.name} 的選項` : '管理隱藏的應用程式'} onClose={onClose}><p className="dialog-description">只變更「{host.name}」在此原型中的清單偏好。</p>{apps.map(a => <div key={a.id} className="app-preference"><h3>{a.name}</h3><label><input type="checkbox" checked={!!preferences[a.id]?.hidden} onChange={e => onChange(a.id, 'hidden', e.target.checked)}/>隱藏應用程式</label><label><input type="checkbox" checked={!!preferences[a.id]?.direct} onChange={e => onChange(a.id, 'direct', e.target.checked)}/>下次直接啟動（略過連線流程預覽）</label></div>)}<div className="dialog-actions"><Button primary onClick={onClose}>完成</Button></div></Dialog>;
}

export function AboutDialog({ onClose }) {
  return <Dialog title="關於 Moonlight V+ Lite" onClose={onClose}><div className="about-brand"><Icon name="gamepad" size={42}/><h3>Moonlight V+ Lite</h3></div><p>這是精簡介面的需求確認原型，不是可連線的串流用戶端。</p><p className="help-text">正式版仍使用原生串流核心。GPUI 開發會在需求確認後另行進行。</p><section className="legal-notice"><h3>授權與第三方資訊</h3><p>原專案以 GNU GPLv3 發布；授權條款與第三方聲明保留。</p><div className="button-row"><a className="text-button" href="./LICENSE.txt" target="_blank" rel="noreferrer">GNU GPLv3 授權全文</a><a className="text-button" href="./third-party-notices.txt" target="_blank" rel="noreferrer">第三方聲明</a></div></section><div className="dialog-actions"><Button onClick={onClose}>關閉</Button></div></Dialog>;
}
