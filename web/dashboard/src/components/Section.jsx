import { Children, cloneElement, useState } from 'react';
function Section({ id, header, children }) {
  const [open, setOpen] = useState(() => { try { return localStorage.getItem('intelengine.section.' + id) !== 'closed'; } catch { return true; } });
  return <details className="dashboard-section" open={open} onToggle={event => { const value = event.currentTarget.open; setOpen(value); try { localStorage.setItem('intelengine.section.' + id, value ? 'open' : 'closed'); } catch {} }}><summary onClick={event => { if (event.target.closest('[data-expand-section]')) setOpen(true); if (event.target.closest('button, a, input, select, textarea')) event.preventDefault(); }}>{header}</summary>{children}</details>;
}
export default function withSections(renderTab) {
  return function CollapsibleTab(props) { const view = renderTab(props);
    let n = 0;
    const cards = Children.toArray(view.props.children).map(node => {
      if (node?.type !== 'section') return node;
      const [header, ...body] = Children.toArray(node.props.children);
      const heading = header.type === 'h2' ? header : Children.toArray(header.props.children).find(child => child.type === 'h2');
      return cloneElement(node, { 'data-n': ++n }, <Section id={Children.toArray(heading.props.children).find(child => typeof child === 'string').trim()} header={header}>{body}</Section>);
    });
    const page = view.props.className || '';
    return /\bie-page\b/.test(page) && !/\bie-page-wide\b/.test(page) ? twoColumns(view, cards) : cloneElement(view, {}, cards);
  }; }
// A two-column page gets one box per column, each stacking its own cards, so a folded card frees its space
// (in a grid row it kept the height of the open card beside it). Cards keep the grid's places: 1, 3, ... left.
export function twoColumns(view, cards = Children.toArray(view.props.children)) {
  if (cards.length < 2) return cloneElement(view, {}, cards);
  return cloneElement(view, {}, [0, 1].map(col => <div key={col} className="ie-col">{cards.filter((_, i) => i % 2 === col)}</div>));
}
