import './MissionHeader.css'
import SectionIcon from './SectionIcon.jsx'

// Turns a number of seconds into text like "T+00:58:12".
function formatElapsed(totalSeconds) {
  const hours = String(Math.floor(totalSeconds / 3600)).padStart(2, '0')
  const minutes = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0')
  const seconds = String(totalSeconds % 60).padStart(2, '0')
  return `T+${hours}:${minutes}:${seconds}`
}

// missionName and elapsedSeconds (the time since the robot started) are null until the robot answers.
// connection: { text, live } for the label on the right; live fills in its dot.
// pages: [{ id, label, icon }], the pages to switch between (map, camera, photos, GPS), shown as a row of tabs along
// the bottom of the header; page is the open one, and onPage(id) opens another. On phones the tabs under the header
// (MobileTabs) do this instead.
function MissionHeader({
  missionName,
  elapsedSeconds,
  connection,
  onOpenGlobe,
  pages,
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
          <span className="segment-value">{missionName ?? '—'}</span>
        </p>
        <p className="segment">
          <span className="segment-label">Time since the robot started</span>
          <span className="segment-value mono">{elapsedSeconds === null ? '—' : formatElapsed(elapsedSeconds)}</span>
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
        <p className={connection.live ? 'connection-label live' : 'connection-label'} role="status">
          <span className="connection-dot" aria-hidden="true"></span>
          {connection.text}
        </p>
      </div>

      <nav className="page-nav" aria-label="Pages">
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
    </header>
  )
}

export default MissionHeader
