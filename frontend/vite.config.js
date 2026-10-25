import http from 'node:http'
import https from 'node:https'
import react from '@vitejs/plugin-react'
import { defineConfig, loadEnv } from 'vite'

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  // Requests to /ws and /api are forwarded to the robot's dashboard bridge, so the dashboard can use the same
  // address for everything. ./prowl serves the built dashboard on the robot itself and sets PROWL_ROBOT to the
  // bridge next to it. To run `npm run dev` on another computer, put this line in a file called .env.local next
  // to this one:
  //   PROWL_ROBOT=http://<robot address>:8000
  const env = loadEnv(mode, process.cwd(), '')
  const robot = env.PROWL_ROBOT || 'http://localhost:8000'

  // Keeps the connections to the robot open between requests. Without it the proxy closes each one, and the
  // browser has to open a new connection for every request, e.g. every frame on the Camera page.
  const agent = new (robot.startsWith('https:') ? https : http).Agent({ keepAlive: true })

  const proxy = {
    '/api': { target: robot, changeOrigin: true, agent },
    '/ws': { target: robot, ws: true, changeOrigin: true },
  }

  return {
    plugins: [react()],
    server: { proxy },
    preview: { proxy },
  }
})
