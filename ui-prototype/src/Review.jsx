import { useState } from 'react';
import { Button, Icon } from './components';
import { optionalFeatures, scopeGroups, settingsCoverage } from './model';

export default function Review({ decisions, onDecision, notes, setNotes, exportReview, onBack }) {
  const [exported, setExported] = useState(false);
  const pending = optionalFeatures.filter(f => !decisions[f.id] || decisions[f.id] === 'pending').length;
  return <main className="review-page" id="main-content"><header className="review-title"><div><h1>需求範圍</h1><p>先確認這份 Web 原型，再決定 GPUI 開發範圍。</p></div><Button onClick={onBack}>返回介面</Button></header>
    <div className="review-notice"><Icon name="info"/><p>這些選擇只會儲存在本瀏覽器，並更新原型中的入口。尚未刪除正式版功能，也不會自動開始 GPUI 遷移。</p></div>
    <div className="scope-groups">{scopeGroups.map(group => <section key={group.title}><h2><Icon name={group.status === 'keep' ? 'check' : group.status === 'remove' ? 'close' : 'sliders'}/>{group.title}</h2><ul>{group.items.map(item => <li key={item}>{item}</li>)}</ul></section>)}</div>
    <section className="decision-section"><h2>縮減範圍已確認 <span>{pending} 項待決定</span></h2><p className="help-text">已確認 Windows PC 專用、三語、保留遊戲隱藏與直啟；移除 SBS 強制拉伸及客戶端手把螢幕鍵盤。</p>
      {optionalFeatures.map(feature => <div className="decision-row" key={feature.id}><div><h3>{feature.name}</h3><p>{feature.impact}</p></div><fieldset aria-label={feature.name}><legend className="sr-only">{feature.name}</legend>{[['pending', '待決定'], ['keep', '保留'], ['remove', '移除']].map(([value, label]) => <label key={value}><input type="radio" name={`decision-${feature.id}`} value={value} checked={(decisions[feature.id] || 'pending') === value} onChange={() => { onDecision(feature.id, value); setExported(false); }}/>{label}</label>)}</fieldset></div>)}
    </section>
    <section className="review-followup"><h2>設定對照與原型限制</h2><p>依目前儲存庫 Lite 的 QML 設定逐類核對。下表區分已列出的選項與尚未重製的行為；不是已核准的刪除清單。</p><div className="coverage-table-wrap"><table className="coverage-table"><thead><tr><th>原版分類</th><th>原型已列選項</th><th>尚未重製／差異</th></tr></thead><tbody>{settingsCoverage.map(row => <tr key={row.category}><th scope="row">{row.category}<small>{row.source}</small></th><td>{row.coverage}</td><td>{row.limitation}</td></tr>)}</tbody></table></div></section>
    <section className="review-followup"><h2>仍需驗證的 GPUI 行為</h2><ul><li>繁中輸入法、鍵盤焦點與手把導航。</li><li>多螢幕、全螢幕切換，以及串流結束後回到主介面。</li><li>文字／圖片剪貼簿與原生串流控制選單。</li><li>HDR 詳細亮度偵測／校正、語言切換與主機能力判斷。</li><li>設定及舊主機資料的相容與遷移。</li></ul><p>Web 原型可確認資訊架構與操作流程；不能用來驗證解碼效能、實際配對或主機相容性。</p></section>
    <label className="notes-label" htmlFor="review-notes">你的調整意見</label><textarea id="review-notes" value={notes} onChange={e => { setNotes(e.target.value); setExported(false); }} placeholder="例如：我希望螢幕切換保留在首頁；這個選項應該更容易找到。" rows={4}/>
    <div className="review-export"><span role="status">{exported ? '已產生需求確認 JSON；未傳送至任何服務。' : '可匯出選擇、意見及目前的設定範例。'}</span><Button primary onClick={() => { exportReview(); setExported(true); }}><Icon name="download" size={18}/>匯出需求確認</Button></div>
  </main>;
}
