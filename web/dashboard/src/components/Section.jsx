import { Children, cloneElement, useState } from 'react';
function Section({ id, header, children }) {
  const [open, setOpen] = useState(() => { try { return localStorage.getItem('intelengine.section.' + id) !== 'closed'; } catch { return true; } });
  return <details className="dashboard-section" open={open} onToggle={event => { const value = event.currentTarget.open; setOpen(value); try { localStorage.setItem('intelengine.section.' + id, value ? 'open' : 'closed'); } catch {} }}><summary onClick={event => { if (event.target.closest('button, a, input, select, textarea')) event.preventDefault(); }}>{header}</summary>{children}</details>;
}
export default function withSections(renderTab) {
  return function CollapsibleTab(props) { const view = renderTab(props);
    return cloneElement(view, {}, Children.map(view.props.children, node => {
      if (node?.type !== 'section') return node;
      const [header, ...body] = Children.toArray(node.props.children);
      const heading = header.type === 'h2' ? header : Children.toArray(header.props.children).find(child => child.type === 'h2');
      return cloneElement(node, {}, <Section id={Children.toArray(heading.props.children).find(child => typeof child === 'string').trim()} header={header}>{body}</Section>);
    }));
  }; }
