import './MissionHeader.css'
import SectionIcon from './SectionIcon.jsx'

// clockLabel and connection say where the data comes from (demo, simulator or robot); see src/live/labels.js
// pages (robot only): [{ id, label, icon }], the pages to switch between (map, camera, photos), shown as a row of tabs
// along the bottom of the header; page is the open one, and onPage(id) opens another. On phones the tabs under the
// header (MobileTabs) do this instead.
function MissionHeader({
  mission,
  elapsedText,
  clockLabel = 'Simulated mission time',
  connection = { text: 'Simulation — robot not connected', live: false },
  onOpenGlobe,
  pages = null,
  page,
  onPage,
  colorBlind,
  onToggleColorBlind,
}) {
  return (
    <header className="topbar">
      <div className="brand">
        <span className="brand-mark" aria-hidden="true"></span>
        <h1>
          Prowl <span className="divider">|</span> Material Reconnaissance
        </h1>
      </div>

      {/* Small label above a larger value, with thin lines between them */}
      <div className="topbar-info">
        <p className="segment">
          <span className="segment-label">Mission</span>
          <span className="segment-value">{mission.name}</span>
        </p>
        <p className="segment">
          <span className="segment-label">{clockLabel}</span>
          <span className="segment-value mono">{elapsedText}</span>
        </p>
        <button className="button header-button" onClick={onOpenGlobe}>
          <SectionIcon name="moon" />
          Moon globe
        </button>
        {/* aria-pressed tells screen readers this is an on/off switch. The words ON and OFF mean it never relies on color. */}
        <button className="button header-button" onClick={onToggleColorBlind} aria-pressed={colorBlind}>
          <SectionIcon name="eye" />
          Color-blind mode
          <span className={colorBlind ? 'switch-state on' : 'switch-state'}>{colorBlind ? 'On' : 'Off'}</span>
        </button>
        <p className={connection.live ? 'sim-label live' : 'sim-label'} role="status">
          <span className="sim-icon" aria-hidden="true"></span>
          {connection.text}
        </p>
      </div>

      {pages && (
        <nav className="page-nav" aria-label="Robot pages">
          {pages.map((entry) => (
            <button
              key={entry.id}
              className={entry.id === page ? 'page-tab active' : 'page-tab'}
              onClick={() => onPage(entry.id)}
              aria-current={entry.id === page ? 'page' : undefined}
            >
              <SectionIcon name={entry.icon} />
              {entry.label}
            </button>
          ))}
        </nav>
      )}
    </header>
  )
}

export default MissionHeader
