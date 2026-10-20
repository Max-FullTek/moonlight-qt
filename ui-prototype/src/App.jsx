import { useEffect, useState } from 'react';
import Hosts from './Hosts';
import Settings from './Settings';
import Review from './Review';
import { AboutDialog, AddHostDialog, AppOptionsDialog, HostOptionsDialog, PairDialog } from './Dialogs';
import { Button, Dialog, Icon } from './components';
import { applications, confirmedDecisions, defaults, initialHosts, loadState, optionalFeatures, scopeGroups, settingsCoverage, STORAGE_KEY } from './model';

function initialState() {
  const restored = loadState();
  return { version: 1, hosts: restored.hosts ?? initialHosts, settings: { ...defaults, ...restored.settings }, decisions: confirmedDecisions, notes: restored.notes || '', appPreferences: restored.appPreferences || {} };
}

export default function App() {
  const [state, setState] = useState(initialState);
  const [draft, setDraft] = useState(() => ({ ...state.settings }));
  const [page, setPage] = useState('hosts');
  const [lastPage, setLastPage] = useState('hosts');
  const [category, setCategory] = useState('stream');
  const [selectedId, setSelectedId] = useState(() => state.hosts[0]?.id);
  const [deviceOpen, setDeviceOpen] = useState(false);
  const [modal, setModal] = useState(null);
  const [session, setSession] = useState(null);
  const [toast, setToast] = useState('');
  const [storageError, setStorageError] = useState(false);
  const [sessionStats, setSessionStats] = useState(false);
  useEffect(() => {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); setStorageError(false); }
    catch { setStorageError(true); }
  }, [state]);
  const close = () => setModal(null);
  const updateHost = host => setState(previous => ({ ...previous, hosts: previous.hosts.map(h => h.id === host.id ? host : h) }));
  const selectedHost = modal?.host || state.hosts.find(h => h.id === selectedId);
  const selectDevice = id => {
    const host = state.hosts.find(h => h.id === id);
    setSelectedId(id);
    setDeviceOpen(true);
    if (host?.online && !host.paired) setModal({ type: 'pair', host });
  };
  const openSettings = () => { setPage('settings'); setCategory('stream'); };
  const openReview = () => { if (page !== 'review') setLastPage(page); setPage('review'); };
  const start = (host, app, resume = false) => {
    if (session && (session.hostId !== host.id || session.appId !== app.id)) {
      setModal({ type: 'switchApp', host, app });
      return;
    }
    if (state.decisions.appOptions !== 'remove' && state.appPreferences[host.id]?.[app.id]?.direct) {
      setSession({ hostId: host.id, appId: app.id });
      setModal({ type: 'streaming', host, app });
    } else setModal({ type: 'connect', host, app, resume });
  };
  const exportReview = () => {
    const payload = {
      schemaVersion: 1, prototypeVersion: '0.1.0', exportedAt: new Date().toISOString(),
      scope: scopeGroups, settingsCoverage, decisions: optionalFeatures.map(f => ({ ...f, decision: state.decisions[f.id] || 'pending' })),
      notes: state.notes, sampleSettings: state.settings,
      implementationStarted: false,
    };
    const url = URL.createObjectURL(new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' }));
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'moonlight-lite-requirements.json'; anchor.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  return <div className="prototype-root">
    <a href="#main-content" className="skip-link">跳至主要內容</a>
    <div className="prototype-bar"><span><Icon name="info" size={16}/>互動原型 · 不會連線真實主機</span><button onClick={openReview} className={page === 'review' ? 'active' : ''}>需求範圍</button></div>
    <div className="application-shell"><header className="app-header"><div className="brand"><Icon name="gamepad" size={29}/><span>Moonlight V+ Lite</span></div><nav aria-label="主要導覽"><button aria-current={page === 'hosts' ? 'page' : undefined} className={page === 'hosts' ? 'active' : ''} onClick={() => { setPage('hosts'); setDeviceOpen(false); }}><Icon name="monitor"/>裝置</button><button aria-current={page === 'settings' ? 'page' : undefined} className={page === 'settings' ? 'active' : ''} onClick={() => setPage('settings')}><Icon name="sliders"/>設定</button></nav><button className="about-button" onClick={() => setModal({ type: 'about' })}><Icon name="info"/>關於</button></header>
      {storageError ? <p className="storage-warning" role="alert">瀏覽器無法儲存原型資料；本次操作仍可繼續，但重新整理後可能不會保留。</p> : null}
      {toast ? <div className="inline-notice" role="status"><Icon name="check" size={17}/><span>{toast}</span><button aria-label="關閉提示" onClick={() => setToast('')}><Icon name="close" size={16}/></button></div> : null}
      {page === 'hosts' ? <Hosts hosts={state.hosts} selectedId={selectedId} deviceOpen={deviceOpen} onBack={() => setDeviceOpen(false)} onSelect={selectDevice} onAdd={() => setModal({ type: 'add' })} onOptions={host => setModal({ type: 'host', host })} onPair={host => setModal({ type: 'pair', host })} onWake={host => { updateHost({ ...host, online: true }); setToast('已將範例主機切換為在線；未發送網路封包。'); }} onStart={start} onAppOptions={(host, app) => setModal({ type: 'appOptions', host, app })} settings={state.settings} decisions={state.decisions} appPreferences={state.appPreferences} session={session} onSettings={openSettings}/> : page === 'settings' ? <Settings saved={state.settings} draft={draft} setDraft={setDraft} onSave={settings => setState(previous => ({ ...previous, settings: { ...settings } }))} decisions={state.decisions} category={category} setCategory={setCategory} onCoverage={openReview}/> : <Review decisions={state.decisions} onDecision={(id, value) => setState(previous => ({ ...previous, decisions: { ...previous.decisions, [id]: value } }))} notes={state.notes} setNotes={notes => setState(previous => ({ ...previous, notes }))} exportReview={exportReview} onBack={() => setPage(lastPage)}/>}
    </div>
    {modal?.type === 'add' ? <AddHostDialog hosts={state.hosts} oskEnabled={false} onClose={close} onAdd={host => { setState(previous => ({ ...previous, hosts: [...previous.hosts, host] })); setSelectedId(host.id); setDeviceOpen(true); setModal({ type: 'pair', host }); }}/> : null}
    {modal?.type === 'pair' ? <PairDialog host={selectedHost} onClose={close} onSuccess={() => { updateHost({ ...selectedHost, paired: true }); setToast(`「${selectedHost.name}」已完成模擬配對。`); close(); }} onFailure={() => setModal({ type: 'error', title: '配對未完成', message: 'PIN 未被確認或連線逾時。正式版會顯示主機回報的原因。', retry: { type: 'pair', host: selectedHost } })}/> : null}
    {modal?.type === 'host' ? <HostOptionsDialog host={selectedHost} onClose={close} onSave={host => { updateHost(host); close(); }} onDelete={() => setModal({ type: 'delete', host: selectedHost })}/> : null}
    {modal?.type === 'delete' ? <Dialog title="移除範例主機" onClose={close}><p>確定從此原型移除「{selectedHost.name}」？不會影響 Moonlight 的主機或配對資料。</p><div className="dialog-actions"><Button onClick={close} autoFocus>取消</Button><Button danger onClick={() => { setState(previous => ({ ...previous, hosts: previous.hosts.filter(h => h.id !== selectedHost.id) })); if (session?.hostId === selectedHost.id) setSession(null); setDeviceOpen(false); close(); }}>移除</Button></div></Dialog> : null}
    {modal?.type === 'appOptions' ? <AppOptionsDialog host={selectedHost} app={modal.app} preferences={state.appPreferences[selectedHost.id] || {}} onClose={close} onChange={(appId, key, value) => setState(previous => ({ ...previous, appPreferences: { ...previous.appPreferences, [selectedHost.id]: { ...previous.appPreferences[selectedHost.id], [appId]: { ...previous.appPreferences[selectedHost.id]?.[appId], [key]: value } } } }))}/> : null}
    {modal?.type === 'about' ? <AboutDialog onClose={close}/> : null}
    {modal?.type === 'connect' ? <Dialog title={modal.resume ? '恢復串流' : '開始串流'} onClose={close}><div className="connection-state"><Icon name="monitor" size={42}/><h3>{modal.app.name}</h3><p>{selectedHost.name} · {selectedHost.address}</p><div className="connection-stages"><span><Icon name="check" size={16}/>確認主機</span><span><Icon name="check" size={16}/>載入應用程式</span><span>等待串流視窗</span></div></div><p className="help-text">此處使用明確的模擬按鈕確認成功與失敗流程，不會真的啟動主機程式。</p><div className="dialog-actions wrap"><Button onClick={close}>取消</Button><Button onClick={() => setModal({ type: 'error', title: '無法開始串流', message: '範例錯誤：無法建立與主機的連線。請確認 Sunshine、位址及網路連通性。', retry: modal })}>模擬連線失敗</Button><Button primary onClick={() => { setSession({ hostId: selectedHost.id, appId: modal.app.id }); setSessionStats(state.settings.stats); setModal({ ...modal, type: 'streaming' }); }}>模擬連線成功</Button></div></Dialog> : null}
    {modal?.type === 'streaming' ? <Dialog title="串流已連線（模擬）" onClose={close}><div className="connection-state"><Icon name="monitor" size={48}/><h3>{modal.app.name}</h3><p>正式版會切換至原生串流視窗，並隱藏主介面。</p></div><p className="help-text">影片、音效、剪貼簿與輸入傳送不在 Web 原型內實作。</p><div className="stream-controls"><label><input type="checkbox" checked={sessionStats} onChange={e => setSessionStats(e.target.checked)}/>顯示效能統計（僅預覽開關）</label><Button onClick={() => setModal({ ...modal, type: 'disconnect' })}>結束串流</Button></div></Dialog> : null}
    {modal?.type === 'disconnect' ? <Dialog title="結束串流" onClose={close}><p>選擇只中斷連線，或連同主機應用程式一起結束。</p><p className="warning-text"><Icon name="warning" size={18}/>關閉主機程式可能失去未儲存進度。</p><div className="dialog-actions wrap"><Button autoFocus onClick={() => setModal({ ...modal, type: 'streaming' })}>返回串流</Button><Button danger onClick={() => { setSession(null); close(); setToast('已模擬關閉主機程式；未對真實主機執行任何操作。'); }}>模擬關閉主機程式</Button><Button primary onClick={() => { close(); setToast('已中斷模擬串流，主機程式保留，可從清單恢復。'); }}>只中斷連線</Button></div></Dialog> : null}
    {modal?.type === 'switchApp' ? <Dialog title="主機已有程式執行中" onClose={close}><p>開始「{modal.app.name}」前，需要先結束目前的程式。未儲存進度可能遺失。</p><div className="dialog-actions"><Button autoFocus onClick={close}>取消</Button><Button danger onClick={() => { setSession(null); setModal({ ...modal, type: 'connect' }); }}>模擬結束並切換</Button></div></Dialog> : null}
    {modal?.type === 'error' ? <Dialog title={modal.title} onClose={close}><p className="dialog-description">{modal.message}</p><div className="dialog-actions"><Button onClick={close}>關閉</Button><Button primary onClick={() => setModal(modal.retry)}>重試</Button></div></Dialog> : null}
  </div>;
}
