import { Children, useEffect, useId, useLayoutEffect, useRef, useState } from 'react';

// Shared by selects and the searchable faction picker; geometry is in viewport pixels.
export function useDropdown(open, setOpen, content) {
  const ref = useRef(null);
  const menuRef = useRef(null);
  const [placement, setPlacement] = useState({ openUp: false, maxHeight: 128 });

  useEffect(() => {
    if (!open) return;
    const outside = e => { if (!ref.current?.contains(e.target)) setOpen(false); };
    const dismiss = e => { if (!menuRef.current?.contains(e.target)) setOpen(false); };
    const resize = () => setOpen(false);
    const escape = e => {
      if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setOpen(false); }
    };
    document.addEventListener('mousedown', outside);
    document.addEventListener('focusin', outside);
    document.addEventListener('keydown', escape);
    document.addEventListener('scroll', dismiss, true);
    window.addEventListener('resize', resize);
    window.magelight?.send('__escapecapture', '1');
    return () => {
      document.removeEventListener('mousedown', outside);
      document.removeEventListener('focusin', outside);
      document.removeEventListener('keydown', escape);
      document.removeEventListener('scroll', dismiss, true);
      window.removeEventListener('resize', resize);
      window.magelight?.send('__escapecapture', '0');
    };
  }, [open, setOpen]);

  useLayoutEffect(() => {
    if (!open || !menuRef.current) return;
    const anchor = ref.current.getBoundingClientRect();
    const scale = anchor.height / ref.current.offsetHeight;
    let top = 0, bottom = window.innerHeight;
    // Root overflow clips the viewport, not the body's (fixed-content) layout height.
    for (let parent = ref.current.parentElement; parent && parent !== document.body && parent !== document.documentElement; parent = parent.parentElement) {
      if (/(auto|scroll|hidden|clip)/.test(getComputedStyle(parent).overflowY)) {
        const bounds = parent.getBoundingClientRect();
        const parentScale = bounds.height / parent.offsetHeight;
        top = Math.max(top, bounds.top + parent.clientTop * parentScale);
        bottom = Math.min(bottom, bounds.top + (parent.clientTop + parent.clientHeight) * parentScale);
      }
    }
    const below = bottom - anchor.bottom, above = anchor.top - top;
    const height = Math.min(128, menuRef.current.scrollHeight + 2) * scale + 2 * scale;
    const openUp = below < height && (above >= height || above > below);
    setPlacement({ openUp, maxHeight: Math.max(0, Math.min(128, (openUp ? above : below) / scale - 2)) });
  }, [open, content]);

  return { ref, menuRef, ...placement };
}

export const dropdownClass = 'absolute z-50 left-0 right-0 overflow-y-auto bg-[#1a1a2e] border border-white/10 rounded shadow-lg';

export default function Select({ value, onChange, children, className = '', containerClassName = '', disabled = false, 'aria-label': label }) {
  const allOptions = Children.toArray(children);
  const optionText = option => Children.toArray(option?.props.children).join('');
  const selectedOption = allOptions.find(option => String(option.props.value) === String(value));
  const [query, setQuery] = useState(null);
  const text = query ?? (String(value) === '' ? '' : optionText(selectedOption));
  const options = allOptions.filter(option => String(option.props.value) !== '' &&
    (query === null || optionText(option).toLowerCase().includes(query.toLowerCase())));
  const selected = options.findIndex(option => String(option.props.value) === String(value));
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const id = useId();
  const controlRef = useRef(null);
  const expanded = open && !disabled;
  const { ref, menuRef, openUp, maxHeight } = useDropdown(expanded, setOpen, options.length);
  const enabled = options.map((option, index) => option.props.disabled ? -1 : index).filter(index => index >= 0);
  const show = () => { setActive(enabled.includes(selected) ? selected : enabled[0]); setOpen(true); };
  const choose = index => {
    if (!options[index] || options[index].props.disabled) return;
    const next = String(options[index].props.value);
    if (next !== String(value)) onChange({ target: { value: next } });
    setQuery(null);
    setOpen(false);
  };
  const keyDown = e => {
    if (e.key === 'Escape' && expanded) { e.preventDefault(); e.stopPropagation(); setOpen(false); }
    else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      if (!expanded) show();
      else if (enabled.length) {
        const position = enabled.indexOf(active);
        setActive(enabled[(position + (e.key === 'ArrowDown' ? 1 : -1) + enabled.length) % enabled.length]);
      }
    } else if (e.key === 'Enter') {
      e.preventDefault();
      if (expanded) choose(active); else show();
    }
  };
  const controlProps = {
    ref: controlRef, role: 'combobox', 'aria-label': label, 'aria-haspopup': 'listbox',
    'aria-expanded': expanded, 'aria-controls': id,
    'aria-activedescendant': expanded && options[active] ? `${id}-${active}` : undefined,
    disabled, onKeyDown: keyDown,
    onBlur: e => { if (!ref.current.contains(e.relatedTarget)) setOpen(false); },
    className: `w-full min-w-0 text-left disabled:opacity-30 disabled:cursor-not-allowed ${className}`,
  };

  useLayoutEffect(() => {
    const menu = menuRef.current, option = menu?.children[active];
    if (!expanded || !option) return;
    if (option.offsetTop < menu.scrollTop) menu.scrollTop = option.offsetTop;
    else if (option.offsetTop + option.offsetHeight > menu.scrollTop + menu.clientHeight)
      menu.scrollTop = option.offsetTop + option.offsetHeight - menu.clientHeight;
  }, [expanded, active, maxHeight]);

  return (
    <div ref={ref} className={`relative min-w-0 ${containerClassName}`}>
      <input {...controlProps} type="text" autoComplete="off" aria-autocomplete="list"
        value={text}
        placeholder={optionText(allOptions.find(option => String(option.props.value) === '')) || `${label}...`}
        style={{ paddingRight: 24 }} onClick={() => expanded ? setOpen(false) : show()}
        onChange={e => {
          setQuery(e.target.value); setActive(0); setOpen(true);
          if (String(value) !== '') onChange({ target: { value: '' } });
        }} />
      {String(text) === '' && <span aria-hidden="true" className="absolute right-2 top-1/2 -translate-y-1/2 text-[9px] text-gray-500 pointer-events-none">▼</span>}
      {String(text) !== '' && (
        <button type="button" aria-label={`Clear ${label}`} disabled={disabled}
          onMouseDown={e => e.preventDefault()} onClick={() => {
            if (String(value) !== '') onChange({ target: { value: '' } });
            setQuery(''); setActive(0); setOpen(true); controlRef.current.focus({ preventScroll: true });
          }} className="absolute right-1 top-1/2 -translate-y-1/2 px-1 text-xs text-gray-500 hover:text-gray-200 disabled:opacity-30">
          ×
        </button>
      )}
      {expanded && (
        <div id={id} role="listbox" ref={menuRef} style={{ maxHeight }}
          className={`${dropdownClass} ${openUp ? 'bottom-full mb-0.5' : 'top-full mt-0.5'}`}>
          {options.map((option, index) => (
            <div key={option.key} id={`${id}-${index}`} role="option" aria-selected={index === selected}
              aria-disabled={option.props.disabled || undefined}
              onMouseDown={e => e.preventDefault()} onMouseEnter={() => { if (!option.props.disabled) setActive(index); }}
              onClick={() => choose(index)}
              className={`px-2 py-1 text-xs truncate ${option.props.disabled ? 'opacity-30' : 'cursor-pointer hover:bg-white/10'} ${index === active ? 'bg-white/10' : ''} ${index === selected ? 'text-blue-300' : 'text-gray-300'}`}>
              {option.props.children}
            </div>
          ))}
          {!options.length && <div className="px-2 py-1 text-xs text-gray-500">No matches</div>}
        </div>
      )}
    </div>
  );
}
