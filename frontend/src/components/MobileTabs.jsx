import './MobileTabs.css'

const tabs = [
  { id: 'map', label: 'Map' },
  { id: 'findings', label: 'Findings' },
  { id: 'review', label: 'Review' },
]

// Only shown on phones. It decides which part of the dashboard is on screen.
// extraTabs (robot only): more tabs after these, for the robot's own pages, e.g. [{ id: 'camera', label: 'Camera' }].
function MobileTabs({ activeTab, onChange, extraTabs = [] }) {
  const shown = [...tabs, ...extraTabs]
  return (
    <nav className="mobile-tabs" aria-label="Dashboard sections" data-count={shown.length}>
      {shown.map((tab) => (
        <button
          key={tab.id}
          className={tab.id === activeTab ? 'mobile-tab active' : 'mobile-tab'}
          onClick={() => onChange(tab.id)}
          aria-current={tab.id === activeTab ? 'page' : undefined}
        >
          {tab.label}
        </button>
      ))}
    </nav>
  )
}

export default MobileTabs
