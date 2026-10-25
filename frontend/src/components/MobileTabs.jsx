import './MobileTabs.css'

// Only shown on phones, in place of the header's row of page tabs. pages: [{ id, label }]; page is the open one,
// and onPage(id) opens another.
function MobileTabs({ pages, page, onPage }) {
  return (
    <nav className="mobile-tabs" aria-label="Pages">
      {pages.map((tab) => (
        <button
          key={tab.id}
          className={tab.id === page ? 'mobile-tab active' : 'mobile-tab'}
          onClick={() => onPage(tab.id)}
          aria-current={tab.id === page ? 'page' : undefined}
        >
          {tab.label}
        </button>
      ))}
    </nav>
  )
}

export default MobileTabs
