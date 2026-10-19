import { applications } from './model';
import { Button, Icon } from './components';

export default function Hosts({ hosts, selectedId, deviceOpen, onBack, onSelect, onAdd, onOptions, onPair, onWake, onStart, onAppOptions, settings, decisions, appPreferences, session, onSettings }) {
  const host = hosts.find(h => h.id === selectedId) || hosts[0];
  const prefs = host ? appPreferences[host.id] || {} : {};
  const apps = decisions.appOptions === 'remove' ? applications : applications.filter(app => !prefs[app.id]?.hidden);
  const isRunning = app => session?.hostId === host?.id && session?.appId === app.id;
  return <div className="devices-layout">
    {!deviceOpen || !host ? <main className="devices-page" id="main-content"><header className="devices-heading"><h1>裝置</h1><Button onClick={onAdd}><Icon name="plus" size={18}/>新增裝置</Button></header>
      {!hosts.length ? <div className="empty-state"><Icon name="monitor" size={52}/><h2>新增你的第一台裝置</h2><p>手動輸入區網或 Tailscale 位址，配對後即可選擇應用程式。</p><Button primary onClick={onAdd}>新增裝置</Button></div> : <div className="device-grid" aria-label="裝置清單">{hosts.map(h => <article key={h.id} className={`device-card ${h.online ? '' : 'offline'}`}><button className="device-open" onClick={() => onSelect(h.id)} aria-label={`連線至 ${h.name}`}><span className="device-art"><Icon name="monitor" size={80}/></span><strong>{h.name}</strong><span className="device-status"><i className={h.online ? 'online' : ''}/>{!h.online ? '離線' : h.paired ? '已配對 · 在線' : '尚未配對'}</span><span className="device-address">{h.address}</span></button><button className="device-options icon-button" aria-label={`${h.name} 的裝置選項`} onClick={() => onOptions(h)}><Icon name="sliders" size={18}/></button></article>)}</div>}
    </main> : <main className="device-apps-page" id="main-content"><button className="back-devices text-button" onClick={onBack}><Icon name="chevron" size={18}/>返回裝置</button>
      {!host ? <div className="empty-state"><Icon name="monitor" size={52}/><h2>新增你的第一台主機</h2><p>手動輸入區網或 Tailscale 位址，配對後即可選擇應用程式。</p><Button primary onClick={onAdd}><Icon name="plus"/>新增主機</Button></div> : <>
        <div className="selected-host"><div className="selected-host-title"><Icon name="monitor" size={40}/><div><h2>{host.name}</h2><p>{host.address} · 手動連線</p></div></div><Button onClick={() => onOptions(host)}><Icon name="sliders" size={18}/>主機選項</Button></div>
        {!host.online ? <div className="empty-state"><Icon name="monitor" size={48}/><h2>主機目前離線</h2><p>確認主機與 Sunshine 已啟動；使用 Tailscale 時也請確認兩端在線。</p><Button primary onClick={() => onWake(host)}>模擬主機上線</Button><p className="help-text">僅切換範例狀態，不會發送喚醒封包。</p></div> : !host.paired ? <div className="empty-state"><Icon name="screens" size={48}/><h2>先與這台主機配對</h2><p>取得 PIN 後，在主機端 Sunshine 完成配對。</p><Button primary onClick={() => onPair(host)}>配對主機</Button></div> : <>
          {session?.hostId === host.id ? <div className="session-banner"><span><i className="status-dot"/>主機程式仍在執行：{applications.find(a => a.id === session.appId)?.name}</span><Button onClick={() => onStart(host, applications.find(a => a.id === session.appId), true)}>恢復串流</Button></div> : null}
          <div className="app-list" aria-label="應用程式清單">{apps.map(app => <article className="app-item" key={app.id}><Icon name={app.icon} size={40}/><div className="app-info"><h3>{app.name}{prefs[app.id]?.direct ? <small>直接啟動</small> : null}</h3><p>{app.description}</p></div><div className="app-actions">{decisions.appOptions !== 'remove' ? <button className="icon-button" aria-label={`${app.name} 的選項`} onClick={() => onAppOptions(host, app)}><Icon name="sliders" size={18}/></button> : null}<Button primary onClick={() => onStart(host, app, isRunning(app))}><Icon name="play" size={16}/>{isRunning(app) ? '恢復串流' : '開始串流'}</Button></div></article>)}</div>
          {decisions.appOptions !== 'remove' && apps.length < applications.length ? <Button className="restore-apps" onClick={() => onAppOptions(host, null)}>管理隱藏的應用程式</Button> : null}
          <div className="stream-summary"><Icon name="sliders"/><span>{settings.resolution === 'custom' ? `${settings.customWidth}×${settings.customHeight}` : settings.resolution === '1920x1080' ? '1080p' : settings.resolution.replace('x', '×')} · {settings.fps === 'custom' ? settings.customFps : settings.fps} FPS · {settings.bitrate} Mbps</span><button className="text-button" onClick={onSettings}>調整串流設定</button></div>
        </>}
      </>}
    </main>}
    <footer className="host-footer"><Icon name="info" size={17}/><span>{settings.discovery ? '區網自動搜尋已開啟（此原型僅預覽狀態，不會掃描網路）。' : '區網自動搜尋已關閉，可手動新增區網或 Tailscale 位址。'}</span></footer>
  </div>;
}
