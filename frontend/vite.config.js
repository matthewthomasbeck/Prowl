import http from 'node:http'
import https from 'node:https'
import react from '@vitejs/plugin-react'
import { defineConfig, loadEnv } from 'vite'

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  // During `npm run dev`, requests to /ws and /api are forwarded to the Prowl backend, so the dashboard
  // can use the same address for everything. By default that's a backend running on this computer.
  // To use the EC2 server instead, put this line in a file called .env.local next to this one:
  //   PROWL_BACKEND=http://<ec2-public-ip>:8000
  const env = loadEnv(mode, process.cwd(), '')
  const backend = env.PROWL_BACKEND || 'http://localhost:8000'

  // Keeps the connections to the backend (or the robot) open between requests. Without it the proxy closes each one,
  // and the browser has to open a new connection for every request, e.g. every frame on the robot's Camera page.
  const agent = new (backend.startsWith('https:') ? https : http).Agent({ keepAlive: true })

  const proxy = {
    '/api': { target: backend, changeOrigin: true, agent },
    '/ws': { target: backend, ws: true, changeOrigin: true },
  }

  return {
    plugins: [react()],
    server: { proxy },
    preview: { proxy },
  }
})
