import './SampleInspector.css'
import StatusBadge from './StatusBadge.jsx'
import SectionIcon from './SectionIcon.jsx'
import { labelsFor } from '../live/labels.js'

// What the queue area says (labels.queueMessages), depending on whether this sample's report is queued yet,
// and on where the data comes from: a local demo list, or the backend's shared mission log.

function SampleInspector({
  sample,
  statusOptions,
  queueState, // 'none' (not queued), 'queued' (queued and unchanged), or 'changed' (queued, then edited)
  isDrawer, // true on iPad, where this panel slides in from the side
  isOpen,
  headingRef,
  onClose,
  onChangeNote,
  onChangeStatus,
  onQueue,
  labels = labelsFor('demo'),
}) {
  const isObstacle = sample.kind === 'obstacle'
  const queueMessages = labels.queueMessages

  const queueLabel =
    queueState === 'changed' ? 'Queue updated report for Earth review' : 'Queue report for Earth review'

  return (
    <aside
      className={isOpen ? 'panel inspector open' : 'panel inspector'}
      role={isDrawer ? 'dialog' : undefined}
      aria-label="Sample inspector"
    >
      <div className="panel-heading">
        <h2 ref={headingRef} tabIndex={-1}>
          <SectionIcon name="scan" />
          Investigation
        </h2>
        <button className="button close-button" onClick={onClose}>
          Close
        </button>
      </div>

      <div className="inspector-body">
        {/* key={sample.id} makes React rebuild this group when a different sample is picked,
            which replays the "scan in" animation: each block fades in one after another. */}
        <div key={sample.id} className="reveal-group">
          <div className="sample-head">
            <p className="sample-id mono">{sample.id}</p>
            <p className="sample-title">{sample.title}</p>
            <p className="sample-badges">
              <StatusBadge status={sample.status} />
              <span className="tag">{isObstacle ? labels.obstacleTag : labels.sampleTag}</span>
            </p>
          </div>

          <dl className="meta">
            <div>
              <dt className="field-label">Discovery time</dt>
              <dd className="mono">{sample.discoveredAt}</dd>
            </div>
            <div>
              <dt className="field-label">Local coordinates</dt>
              <dd className="mono">
                x {sample.x}, y {sample.y}
              </dd>
            </div>
          </dl>

          <section className="info-block">
            <h3 className="field-label">Observed</h3>
            <p>{sample.observed}</p>
          </section>

          <section className="info-block">
            <h3 className="field-label">{isObstacle ? 'Why it matters' : 'Possible value'}</h3>
            <p>{sample.possibleValue}</p>
          </section>

          <section className="info-block">
            <h3 className="field-label">Still unknown</h3>
            <p>{sample.stillUnknown}</p>
          </section>

          <section className="info-block">
            <h3 className="field-label">Next step</h3>
            <p>{sample.nextStep}</p>
          </section>

          <div className="info-block">
            <label htmlFor="reviewer-note" className="field-label">
              Reviewer note
            </label>
            <textarea
              id="reviewer-note"
              value={sample.note}
              onChange={(event) => onChangeNote(sample.id, event.target.value)}
              placeholder="Write what the Earth team should know about this sample"
            />
          </div>

          <div className="info-block">
            <label htmlFor="review-status" className="field-label">
              Review status
            </label>
            <select
              id="review-status"
              value={sample.status}
              onChange={(event) => onChangeStatus(sample.id, event.target.value)}
            >
              {statusOptions.map((option) => (
                <option key={option} value={option}>
                  {option}
                </option>
              ))}
            </select>
          </div>

          {/* A thin line that sweeps down once, like a scanner */}
          <span className="scan-line" aria-hidden="true"></span>
        </div>
      </div>

      <div className="inspector-footer">
        <button
          className="button button-primary"
          onClick={() => onQueue(sample.id)}
          disabled={queueState === 'queued'}
        >
          {queueLabel}
        </button>
        {/* role="status" makes screen readers announce this message when it changes.
            key={queueState} replays the little "stamp" animation each time the state changes. */}
        <p key={queueState} className={`queue-feedback queue-${queueState}`} role="status">
          {queueMessages[queueState]}
        </p>
      </div>
    </aside>
  )
}

export default SampleInspector
