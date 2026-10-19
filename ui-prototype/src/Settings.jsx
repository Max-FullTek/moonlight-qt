import { useState } from 'react';
import { Advanced, Button, Field, Icon, NumberField, Section, SelectField, ToggleField } from './components';
import { categories, defaults, monitorModes, recommendedBitrate, validateSettings } from './model';

function StreamSettings({ data, set, decisions }) {
  return <>
    <Section title="畫面">
      <SelectField label="解析度" value={data.resolution} onChange={v => set('resolution', v)} options={[
        ['1280x720', '1280 × 720'], ['1920x1080', '1920 × 1080'], ['2560x1440', '2560 × 1440'], ['3840x2160', '3840 × 2160'], ['custom', '自訂解析度'],
      ]}/>
      {data.resolution === 'custom' ? <div className="inset-fields"><NumberField label="自訂寬度" value={data.customWidth} onChange={v => set('customWidth', v)} min={256} max={8192}/><NumberField label="自訂高度" value={data.customHeight} onChange={v => set('customHeight', v)} min={256} max={8192}/></div> : null}
      <SelectField label="影格率" value={data.fps} onChange={v => set('fps', v)} options={['30', '60', '90', '120', '144'].map(v => [v, `${v} FPS`]).concat([['custom', '自訂 FPS']])}/>
      {data.fps === 'custom' ? <NumberField label="自訂 FPS" value={data.customFps} onChange={v => set('customFps', v)} min={1} max={512}/> : null}
      <SelectField label="視訊編碼" value={data.codec} onChange={v => set('codec', v)} options={[['auto', '自動'], ['h264', 'H.264'], ['hevc', 'HEVC（H.265）'], ['av1', 'AV1']]}/>
      <Field label={data.abr ? '位元率上限' : '位元率'} description={data.abr ? 'ABR 最高不超過此值；FEC 會增加額外頻寬。' : undefined}>{id => <div className="bitrate-control"><input id={id} type="range" min="1" max="150" value={data.bitrate} onChange={e => set('bitrate', +e.target.value)}/><div className="bitrate-value"><input aria-label="位元率 Mbps" type="number" min="1" max="150" value={data.bitrate} onChange={e => set('bitrate', e.target.value === '' ? '' : +e.target.value)}/><span>Mbps</span></div><Button onClick={() => set('bitrate', recommendedBitrate(data))}>使用建議值</Button></div>}</Field>
    </Section>
    <Section title="連線穩定性">
      <ToggleField label="自動調整位元率（ABR）" description="需要相容的 Sunshine 主機；網路壅塞時可降低位元率。" value={data.abr} onChange={v => set('abr', v)}/>
      <SelectField label="封包遺失保護（FEC）" description="與 ABR 可獨立使用；實際效果取決於主機支援。" value={data.fec} onChange={v => set('fec', v)} options={[['host', '跟隨主機'], ['auto', '自動'], ['fixed', '固定百分比']]}/>
      {data.fec === 'fixed' ? <NumberField label="固定 FEC 百分比" value={data.fecPercent} min={0} max={100} suffix="%" onChange={v => set('fecPercent', v)}/> : null}
    </Section>
    <Advanced title="進階畫面設定">
      <SelectField label="串流視窗模式" value={data.streamWindow || 'fullscreen'} onChange={v => set('streamWindow', v)} options={[['fullscreen', '全螢幕'], ['borderless', '無邊框視窗'], ['window', '視窗']]}/>
      <ToggleField label="垂直同步（VSync）" description="關閉可降低部分顯示等待，但可能產生畫面撕裂。" value={data.vsync} onChange={v => set('vsync', v)}/>
      <ToggleField label="影格節奏控制" description="減少微頓；需先開啟垂直同步。" value={data.pacing} disabled={!data.vsync} onChange={v => set('pacing', v)}/>
      <ToggleField label="啟用 HDR" description="仍需主機、用戶端顯示器及解碼器支援。" value={data.hdr} onChange={v => set('hdr', v)}/>
      {data.hdr ? <div className="inset-fields"><SelectField label="HDR 格式" value={data.hdrFormat} onChange={v => set('hdrFormat', v)} options={[['pq', 'HDR10（PQ）'], ['hlg', 'HLG']]}/><NumberField label="HDR 峰值亮度" value={data.hdrPeak} min={100} max={10000} suffix="nits" onChange={v => set('hdrPeak', v)}/><NumberField label="HDR SDR 白位準" value={data.hdrWhite} min={50} max={1000} suffix="nits" onChange={v => set('hdrWhite', v)}/><p className="help-text">亮度調整是精簡控制提案；主機／顯示器亮度偵測與詳細校正仍需後續確認。</p></div> : null}
      <ToggleField label="AI 畫質增強" description="依顯示卡與驅動支援；可能增加 GPU 使用量。" value={data.enhancement} onChange={v => set('enhancement', v)}/>
      <ToggleField label="串流渲染比例" value={data.resolutionScale} onChange={v => set('resolutionScale', v)}/>
      {data.resolutionScale ? <NumberField label="渲染比例" value={data.scaleRatio} min={20} max={100} suffix="%" onChange={v => set('scaleRatio', v)}/> : null}
      {decisions.stretch !== 'remove' ? <ToggleField label="AR／XR 的 SBS 畫面延展" description="待確認保留；忽略長寬比，供特定 3D 裝置使用。" value={data.stretch} onChange={v => set('stretch', v)}/> : null}
    </Advanced>
    <Advanced title="音效設定">
      <SelectField label="音效配置" value={data.audio} onChange={v => set('audio', v)} options={[['stereo', '立體聲'], ['5.1', '5.1 聲道'], ['7.1', '7.1 聲道'], ['7.1.4', '7.1.4 聲道']]}/>
      <ToggleField label="串流時靜音主機喇叭" value={data.muteHost} onChange={v => set('muteHost', v)}/>
      <ToggleField label="切換到其他視窗時靜音串流" value={data.muteBackground} onChange={v => set('muteBackground', v)}/>
      <p className="help-text">麥克風此次不啟用；不影響接收主機音效。</p>
    </Advanced>
  </>;
}

function InputSettings({ data, set }) {
  const toggle = (key, label, description) => <ToggleField key={key} label={label} description={description} value={data[key]} onChange={v => set(key, v)}/>;
  return <>
    <Section title="鍵盤與滑鼠">
      {toggle('desktopMouse', '遠端桌面滑鼠模式', '滑鼠可自由移出視窗；一般遊戲建議關閉。')}
      {toggle('localCursor', '顯示本機游標')}
      <SelectField label="攔截系統快捷鍵" value={data.capture} onChange={v => set('capture', v)} options={[['off', '不攔截'], ['fullscreen', '僅全螢幕時'], ['always', '一律攔截']]}/>
    </Section>
    <Section title="手把">
      {toggle('swapFace', '交換 A／B 與 X／Y 按鍵', '切換成 Nintendo 風格的按鍵排列。')}
      <Field label="搖桿死區" description="忽略輕微偏移；0% 會原樣傳送輸入。">{id => <div className="range-control"><input id={id} type="range" min="0" max="30" value={data.deadzone} onChange={e => set('deadzone', +e.target.value)}/><output>{data.deadzone}%</output></div>}</Field>
      <SelectField label="結束串流組合鍵" value={data.quitCombo} onChange={v => set('quitCombo', v)} options={[['default', 'Start + Select + LB + RB（預設）'], ['y', 'Select + LB + RB + Y'], ['a', 'Start + LB + RB + A'], ['b', 'Start + LB + RB + B'], ['xy', 'LB + RB + X + Y'], ['ab', 'LB + RB + A + B']]}/>
      <p className="help-text">一般手把傳送及內建映射資料庫保留，與已移除的 USB/IP 裝置分享無關。</p>
    </Section>
    <Advanced title="進階輸入設定">
      {toggle('swapMouse', '交換滑鼠左右鍵')}{toggle('swapAlt', '交換 Alt 與 Win 鍵')}{toggle('reverseScroll', '反轉滑鼠捲動方向')}
      {toggle('touchpad', '將觸控螢幕作為虛擬觸控板')}{toggle('precisionTouchpad', '使用精確式觸控板輸入', '需要相容的裝置與主機，否則回退至游標輸入。')}
      {toggle('forceGamepad', '強制第一支手把保持連線')}{toggle('gamepadMouse', '長按 Start 使用手把控制滑鼠', '關閉時，長按 Start 可開啟串流控制選單。')}{toggle('backgroundGamepad', '程式在背景時仍接收手把輸入')}
      <SelectField label="DualSense 進階觸覺" value={data.haptics} onChange={v => set('haptics', v)} options={[['physical', '實體 DualSense（原生 HD 觸覺）'], ['emulated', '模擬 DualSense（分析式震動）']]}/>
    </Advanced>
  </>;
}

function HostSettings({ data, set }) {
  return <>
    <Section title="多螢幕控制">
      <SelectField label="螢幕組合模式" description={monitorModes.find(m => m[0] === data.monitorMode)?.[2]} value={data.monitorMode} onChange={v => set('monitorMode', v)} options={monitorModes.map(([key, name]) => [key, name])}/>
      <SelectField label="串流螢幕" description="範例螢幕清單；正式版由主機回報可用螢幕。" value={data.monitor} onChange={v => set('monitor', v)} options={[['auto', '自動／依主機設定'], ['display1', 'DISPLAY 1 · 主螢幕'], ['display2', 'DISPLAY 2 · 第二螢幕']]}/>
      <p className="help-text">多螢幕能力保留，實際控制行為取決於 Sunshine 主機支援。</p>
    </Section>
    <Section title="主機行為">
      <ToggleField label="最佳化遊戲設定以進行串流" description="保留主機相容性旗標；Sunshine 也可能將它用於顯示模式控制。" value={data.optimize} onChange={v => set('optimize', v)}/>
      <ToggleField label="結束串流時也關閉主機應用程式" description="開啟後可能失去未儲存進度。預設只中斷串流，保留主機程式。" value={data.quitHost} onChange={v => set('quitHost', v)}/>
    </Section>
  </>;
}

function OtherSettings({ data, set, decisions }) {
  return <>
    <Section title="網路與診斷">
      <ToggleField label="自動搜尋區網主機" description="預設關閉。此原型只切換顯示狀態，不會掃描網路。" value={data.discovery} onChange={v => set('discovery', v)}/>
      <ToggleField label="串流時顯示效能統計" value={data.stats} onChange={v => set('stats', v)}/>
      <SelectField label="串流控制選單位置" value={data.overlay} onChange={v => set('overlay', v)} options={[['button', '浮動按鈕'], ['top', '頂部邊緣'], ['right', '右側邊緣'], ['left', '左側邊緣'], ['off', '停用']]}/>
    </Section>
    <Advanced title="解碼相容性">
      <SelectField label="視訊解碼器" value={data.decoder} onChange={v => set('decoder', v)} options={[['auto', '自動'], ['hardware', '強制硬體解碼'], ['software', '強制軟體解碼']]}/>
      <ToggleField label="啟用 YUV 4:4:4" description="適合桌面及文字；需主機與解碼器支援。" value={data.yuv444} onChange={v => set('yuv444', v)}/>
    </Advanced>
    <Advanced title="介面與通知">
      <SelectField label="啟動時的視窗模式" value={data.uiWindow} onChange={v => set('uiWindow', v)} options={[['window', '視窗'], ['maximized', '最大化'], ['fullscreen', '全螢幕']]}/>
      <ToggleField label="記住視窗位置與大小" value={data.rememberWindow} onChange={v => set('rememberWindow', v)}/>
      <ToggleField label="顯示連線品質警告" value={data.connectionWarnings} onChange={v => set('connectionWarnings', v)}/>
      <ToggleField label="顯示設定警告" value={data.configurationWarnings} onChange={v => set('configurationWarnings', v)}/>
      <ToggleField label="串流時保持螢幕喚醒" value={data.keepAwake} onChange={v => set('keepAwake', v)}/>
      <p className="help-text">原型使用繁體中文。GPUI 版預計保留繁中／英文；其他語言：{decisions.languages === 'remove' ? '提議移除' : decisions.languages === 'keep' ? '提議保留' : '待確認'}。</p>
    </Advanced>
  </>;
}

export default function Settings({ saved, draft, setDraft, onSave, decisions, category, setCategory }) {
  const [notice, setNotice] = useState('');
  const set = (key, value) => {
    setDraft(previous => ({ ...previous, [key]: value, ...(key === 'vsync' && !value ? { pacing: false } : {}) }));
    setNotice('');
  };
  const dirty = JSON.stringify(saved) !== JSON.stringify(draft);
  const save = () => {
    const error = validateSettings(draft);
    if (error) { setNotice(error); return; }
    onSave(draft);
    setNotice('已儲存至本瀏覽器原型，不會修改 Moonlight 設定。');
  };
  return <div className="settings-layout">
    <nav className="category-nav" aria-label="設定分類">{categories.map(c => <button key={c.id} aria-current={category === c.id ? 'page' : undefined} className={category === c.id ? 'selected' : ''} onClick={() => { setCategory(c.id); setNotice(''); }}><Icon name={c.icon}/>{c.name}</button>)}</nav>
    <main className="settings-content" id="main-content"><header className="page-title"><h1>設定</h1><p>精簡常用選項，進階能力仍然保留</p></header>
      {category === 'stream' ? <StreamSettings data={draft} set={set} decisions={decisions}/> : category === 'input' ? <InputSettings data={draft} set={set}/> : category === 'host' ? <HostSettings data={draft} set={set}/> : <OtherSettings data={draft} set={set} decisions={decisions}/>}
      <footer className="save-bar"><div className="save-message" role="status"><Icon name="info" size={18}/><span>{notice || (dirty ? '有尚未儲存的變更；切換頁面仍保留草稿。' : '設定會在下次串流套用')}</span></div><div className="button-row"><Button onClick={() => { setDraft({ ...defaults }); setNotice('已載入建議值，按「儲存設定」後保留。'); }}>載入建議值</Button><Button primary onClick={save}>儲存設定</Button></div></footer>
    </main>
  </div>;
}
