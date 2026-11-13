import { useCallback, useEffect, useRef, useState } from 'react';

// A question for the player: a text, up to three answers and a time bar that drains. When the time is up the
// marked answer is taken, so a prompt never holds the game. Answers can be picked with the mouse, by touch or a VR
// pointer (large buttons), with the pad (Magelight moves the selection, A takes it), or with the keys 1 2 3,
// the arrow keys and Enter.
function PromptCard({ prompt, zoom, onChoose }) {
  const rootRef = useRef(null);
  const pickedRef = useRef(-1);
  const [picked, setPicked] = useState(-1);
  const options = Array.isArray(prompt.options) ? prompt.options.slice(0, 3) : [];
  const fallback = prompt.timeoutChoice >= 0 && prompt.timeoutChoice < options.length ? prompt.timeoutChoice : options.length - 1;
  const total = prompt.timeout > 0 ? prompt.timeout : 0;
  const [left, setLeft] = useState(total);

  // The view closes on the answer, and a key or pad button that is still down at that moment reaches the game:
  // Enter would open the chat box, A would activate whatever is in front of the player. So an answer is
  // reported a moment after it is picked, and for a key only once that key is up again.
  const settle = useCallback((index, key) => {
    if (pickedRef.current >= 0) return;
    pickedRef.current = index;
    setPicked(index);
    let done = false;
    const onUp = (event) => { if (event.key === key) setTimeout(finish, 60); };
    function finish() {
      if (done) return;
      done = true;
      window.removeEventListener('keyup', onUp, true);
      onChoose(index);
    }
    if (key) {
      window.addEventListener('keyup', onUp, true);
      setTimeout(finish, 700);
    } else {
      setTimeout(finish, 220);
    }
  }, [onChoose]);

  useEffect(() => {
    if (!(total > 0)) return undefined;
    const started = performance.now();
    setLeft(total);
    const timer = setInterval(() => {
      if (pickedRef.current >= 0) { clearInterval(timer); return; }
      const remaining = total - (performance.now() - started) / 1000;
      setLeft(Math.max(0, remaining));
      if (remaining <= 0) {
        clearInterval(timer);
        pickedRef.current = fallback;
        onChoose(fallback);
      }
    }, 100);
    return () => clearInterval(timer);
  }, [prompt.id, total, fallback, onChoose]);

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return undefined;
    const buttons = () => Array.from(root.querySelectorAll('button'));
    buttons()[0]?.focus();
    const onKey = (event) => {
      if (event.ctrlKey || event.altKey || event.metaKey) return;
      const index = ['1', '2', '3'].indexOf(event.key);
      if (index >= 0 && index < options.length) {
        event.preventDefault();
        event.stopPropagation();
        settle(index, event.key);
        return;
      }
      if (event.key === 'Enter' || event.key === ' ') {
        const at = buttons().indexOf(document.activeElement);
        event.preventDefault();
        event.stopPropagation();
        if (at >= 0) settle(at, event.key);
        return;
      }
      const step = { ArrowLeft: -1, ArrowUp: -1, ArrowRight: 1, ArrowDown: 1 }[event.key];
      if (step) {
        event.preventDefault();
        event.stopPropagation();
        const list = buttons();
        const at = Math.max(0, list.indexOf(document.activeElement));
        list[Math.min(Math.max(at + step, 0), list.length - 1)]?.focus();
      }
    };
    window.addEventListener('keydown', onKey, true);
    let detach = null;
    try {
      const pad = window.magelight?.gamepad;
      if (pad?.attachNavigation) detach = pad.attachNavigation({ root, initialFocus: () => buttons()[0], wrap: false });
    } catch (e) {
      console.error('[Prompt] gamepad navigation unavailable:', e);
    }
    return () => {
      window.removeEventListener('keydown', onKey, true);
      try { if (detach) detach(); } catch {}
    };
  }, [prompt.id, options.length, settle]);

  return (
    <div className="ie-prompt-backdrop">
      <div ref={rootRef} className="ie-prompt" role="dialog" aria-label={prompt.title || 'IntelEngine'} style={{ zoom }}>
        <div className="ie-prompt-title">{prompt.title || 'IntelEngine'}</div>
        <div className="ie-prompt-body">{prompt.body}</div>
        <div className="ie-prompt-actions">
          {options.map((label, index) => (
            <button
              key={index}
              className={`ie-prompt-btn${index === 0 ? ' ie-prompt-btn--first' : ''}${index === fallback && total > 0 ? ' ie-prompt-btn--fallback' : ''}${picked === index ? ' ie-prompt-btn--picked' : ''}${picked >= 0 && picked !== index ? ' ie-prompt-btn--passed' : ''}`}
              onClick={() => settle(index)}
            >
              <span className="ie-prompt-key">{index + 1}</span>
              <span>{label}</span>
            </button>
          ))}
        </div>
        {total > 0 && (
          <>
            <div className="ie-prompt-timer">
              <div className="ie-prompt-timer-fill" style={{ transform: `scaleX(${Math.max(0, Math.min(1, left / total))})` }} />
            </div>
            <div className="ie-prompt-hint">
              No answer in {Math.ceil(left)} s counts as &ldquo;{options[fallback]}&rdquo;
            </div>
          </>
        )}
      </div>
    </div>
  );
}

export default PromptCard;
