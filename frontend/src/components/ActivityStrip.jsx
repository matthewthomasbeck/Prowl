import './ActivityStrip.css'
import SectionIcon from './SectionIcon.jsx'

function ActivityStrip({ samples, obstacles, queue }) {
  // Two kinds of events: samples being found, and reports being queued locally.
  const discoveries = samples.map((sample) => ({
    key: `found-${sample.id}`,
    time: sample.discoveredAt,
    text: `${sample.id} found: ${sample.title} (simulated)`,
    flag: null,
  }))

  const flagged = obstacles.map((obstacle) => ({
    key: `obstacle-${obstacle.id}`,
    time: obstacle.discoveredAt,
    text: `${obstacle.id} obstacle flagged: ${obstacle.title} (simulated)`,
    flag: null,
  }))

  // Finds and obstacles are put in time order (these times are written so that plain sorting works).
  const happened = [...discoveries, ...flagged].sort((a, b) => a.time.localeCompare(b.time))

  const queued = queue.map((item, index) => ({
    key: `queued-${index}`,
    time: item.queuedAt,
    text: `${item.sampleId} report queued: ${item.status}${item.note ? ', with note' : ''}`,
    flag: 'Queued locally — demo only',
  }))

  // Newest first, like a game's event feed: queue activity is always on top.
  const events = [...happened, ...queued].reverse()

  return (
    <section className="panel activity-strip">
      <div className="panel-heading">
        <h2>
          <SectionIcon name="log" />
          Mission activity
        </h2>
        <span className="tag">Simulated</span>
      </div>

      <ol className="activity-list">
        {events.map((event) => (
          <li key={event.key} className={event.flag ? 'activity-item queued' : 'activity-item'}>
            <span className="activity-marker" aria-hidden="true">
              ◆
            </span>
            <span className="mono activity-time">{event.time}</span>
            <span className="activity-text">{event.text}</span>
            {event.flag && <span className="activity-flag">{event.flag}</span>}
          </li>
        ))}
      </ol>
    </section>
  )
}

export default ActivityStrip
