import './App.css'

function App() {
  return (
    <div className="page">
      <header className="header">
        <h1>Prowl</h1>
        <p className="status">
          <span className="status-dot"></span>
          Disconnected
        </p>
      </header>

      <p className="demo-banner">Demo mode — robot not connected.</p>

      <main className="layout">
        <section className="panel map-panel">
          <h2>Map</h2>
          <div className="placeholder">Map placeholder</div>
        </section>

        <section className="panel">
          <h2>Camera</h2>
          <div className="placeholder camera-placeholder">Camera placeholder</div>
        </section>

        <section className="panel">
          <h2>Observations</h2>
          <p className="empty-note">Observations will appear here.</p>
        </section>
      </main>
    </div>
  )
}

export default App
