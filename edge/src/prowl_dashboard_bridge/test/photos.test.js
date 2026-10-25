import { test } from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { PhotoStore, photoName } from '../bridge/photos.js'

const quiet = { info() {}, warn() {}, error() {} }

// A tiny JPEG header: start of image, then a start-of-frame segment for a 640 x 480 picture.
function fakeJpeg(width = 640, height = 480) {
  const sof = Buffer.from([0xff, 0xc0, 0x00, 0x11, 0x08, height >> 8, height & 0xff, width >> 8, width & 0xff, 0x03])
  return Buffer.concat([Buffer.from([0xff, 0xd8]), sof, Buffer.alloc(20)])
}

// A fresh folder inside the package's .tmp/ (edge/CLAUDE.md: test files stay in the project).
function tempDir(t) {
  const base = new URL('../.tmp/', import.meta.url).pathname
  mkdirSync(base, { recursive: true })
  const dir = mkdtempSync(join(base, 'photos-'))
  t.after(() => rmSync(dir, { recursive: true, force: true }))
  return dir
}

const settled = () => new Promise((resolve) => setTimeout(resolve, 50))

test('photo names are the UTC time they were taken', () => {
  assert.equal(photoName(Date.UTC(2026, 9, 24, 22, 11, 20, 413) / 1000), 'prowl-20261024-221120-413')
})

test('a new photo is listed and saved, and the same still twice is saved once', async (t) => {
  const dir = tempDir(t)
  const store = new PhotoStore({ dir, maxPhotos: 10, log: quiet })
  const t1 = Date.UTC(2026, 9, 24, 22, 11, 20, 413) / 1000
  const entry = store.add(fakeJpeg(), t1)
  assert.deepEqual(entry, {
    name: 'prowl-20261024-221120-413',
    t: t1,
    width: 640,
    height: 480,
    bytes: fakeJpeg().length,
    url: '/api/camera/photos/prowl-20261024-221120-413.jpg',
  })
  assert.deepEqual(store.read(entry.name), fakeJpeg()) // readable while it's still being written
  store.add(fakeJpeg(), t1)
  await settled()
  assert.deepEqual(readdirSync(dir), ['prowl-20261024-221120-413.jpg'])
  assert.deepEqual(store.find(t1), entry)
  assert.equal(store.find(t1 + 1), null)
})

test('photos already in the folder are found again, newest first, and other files are ignored', async (t) => {
  const dir = tempDir(t)
  writeFileSync(join(dir, 'prowl-20261024-100000-000.jpg'), fakeJpeg(100, 50))
  writeFileSync(join(dir, 'prowl-20261024-120000-000.jpg'), fakeJpeg(200, 100))
  writeFileSync(join(dir, 'notes.txt'), 'not a photo')
  writeFileSync(join(dir, 'holiday.jpg'), fakeJpeg())
  const store = new PhotoStore({ dir, maxPhotos: 10, log: quiet })
  assert.deepEqual(
    store.list().map((photo) => [photo.name, photo.width]),
    [
      ['prowl-20261024-120000-000', 200],
      ['prowl-20261024-100000-000', 100],
    ],
  )
})

test('past max_photos the oldest is deleted, and remove() deletes one', async (t) => {
  const dir = tempDir(t)
  const store = new PhotoStore({ dir, maxPhotos: 2, log: quiet })
  const base = Date.UTC(2026, 9, 24, 12) / 1000
  const names = [0, 1, 2].map((i) => store.add(fakeJpeg(), base + i).name)
  await settled()
  assert.deepEqual(store.list().map((photo) => photo.name), [names[2], names[1]])
  assert.equal(existsSync(join(dir, `${names[0]}.jpg`)), false)

  assert.equal(store.remove(names[1]), true)
  assert.equal(store.remove(names[1]), false)
  assert.equal(store.remove('../../etc/passwd'), false)
  assert.equal(store.read(names[1]), null)
  await settled()
  assert.deepEqual(readdirSync(dir), [`${names[2]}.jpg`])
})
