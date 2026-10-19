import { useEffect, useId, useRef } from 'react';

const paths = {
  monitor: <><rect x="3" y="4" width="18" height="13" rx="1"/><path d="M8 21h8m-4-4v4"/></>,
  gamepad: <><path d="M7 7h10a3 3 0 0 1 3 2l2 9a2 2 0 0 1-3 2l-4-4H9l-4 4a2 2 0 0 1-3-2l2-9a3 3 0 0 1 3-2Z"/><path d="M6 11h4m-2-2v4m7-2h.01m3 2h.01"/></>,
  screens: <><path d="M13 5h8v11h-5m-1 5h5m-2-5v5"/><rect x="2" y="3" width="13" height="12" rx="1"/><path d="M5 19h7m-3-4v4"/></>,
  sliders: <><path d="M3 6h5m4 0h9M3 12h11m4 0h3M3 18h3m4 0h11"/><circle cx="10" cy="6" r="2"/><circle cx="16" cy="12" r="2"/><circle cx="8" cy="18" r="2"/></>,
  info: <><circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10h.01"/></>,
  plus: <path d="M12 4v16M4 12h16"/>,
  chevron: <path d="m9 5 7 7-7 7"/>,
  close: <path d="m6 6 12 12M6 18 18 6"/>,
  play: <path d="m8 4 12 8-12 8Z"/>,
  library: <><rect x="3" y="4" width="4" height="16" rx="1"/><path d="M11 4v16m4-16 5 15"/></>,
  check: <path d="m4 12 5 5L20 6"/>,
  warning: <><path d="m12 3 10 18H2Z"/><path d="M12 9v5m0 3h.01"/></>,
  download: <><path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/></>,
};

export function Icon({ name, size = 20, className = '' }) {
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.65" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" className={className}>{paths[name] || paths.monitor}</svg>;
}

export function Button({ children, primary, danger, className = '', ...props }) {
  return <button className={`button ${primary ? 'primary' : ''} ${danger ? 'danger' : ''} ${className}`} {...props}>{children}</button>;
}

export function Dialog({ title, children, onClose, wide = false }) {
  const ref = useRef(null);
  const titleId = useId();
  useEffect(() => { ref.current?.showModal(); }, []);
  return <dialog ref={ref} className={`dialog ${wide ? 'wide' : ''}`} aria-labelledby={titleId} onCancel={event => { event.preventDefault(); onClose(); }}>
    <header className="dialog-header"><h2 id={titleId}>{title}</h2><button className="icon-button" aria-label="關閉對話框" onClick={onClose}><Icon name="close"/></button></header>
    <div className="dialog-content">{children}</div>
  </dialog>;
}

export function Field({ label, description, children, id }) {
  const generated = useId();
  const fieldId = id || generated;
  return <div className="field-row"><div className="field-copy"><label htmlFor={fieldId}>{label}</label>{description ? <p id={`${fieldId}-hint`}>{description}</p> : null}</div><div className="field-control">{children(fieldId, description ? `${fieldId}-hint` : undefined)}</div></div>;
}

export function SelectField({ label, description, value, onChange, options, disabled = false }) {
  return <Field label={label} description={description}>{(id, hint) => <select id={id} aria-describedby={hint} value={value} onChange={event => onChange(event.target.value)} disabled={disabled}>{options.map(option => { const [key, text] = Array.isArray(option) ? option : [option, option]; return <option key={key} value={key}>{text}</option>; })}</select>}</Field>;
}

export function ToggleField({ label, description, value, onChange, disabled = false }) {
  return <Field label={label} description={description}>{(id, hint) => <input id={id} aria-describedby={hint} type="checkbox" role="switch" className="switch" checked={value} onChange={event => onChange(event.target.checked)} disabled={disabled}/>}</Field>;
}

export function NumberField({ label, value, onChange, min, max, step = 1, suffix = '' }) {
  return <Field label={label}>{id => <div className="number-control"><input id={id} type="number" min={min} max={max} step={step} value={value} onChange={event => onChange(event.target.value === '' ? '' : Number(event.target.value))}/>{suffix ? <span>{suffix}</span> : null}</div>}</Field>;
}

export function Section({ title, children }) {
  return <section className="settings-section"><h2>{title}</h2>{children}</section>;
}

export function Advanced({ title, children }) {
  return <details className="advanced"><summary><Icon name="chevron" size={18}/>{title}</summary><div>{children}</div></details>;
}
