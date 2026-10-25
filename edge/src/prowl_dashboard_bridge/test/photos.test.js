import { test } from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { PhotoStore, jpegSize, photoName } from '../bridge/photos.js'

const quiet = { info() {}, warn() {}, error() {} }

// The start of a JPEG: SOI, an APP0 segment, then a baseline frame header (SOF0) for width x height.
function jpegHeader(width = 640, height = 480, sof = 0xc0) {
  const app0 = [0xff, 0xe0, 0x00, 0x06, 0x4a, 0x46, 0x49, 0x46]
  const sofSegment = [0xff, sof, 0x00, 0x0b, 0x08, height >> 8, height & 0xff, width >> 8, width & 0xff, 0x01, 0x01, 0x11, 0x00]
  return Buffer.from([0xff, 0xd8, ...app0, ...sofSegment, 0xff, 0xd9])
}

// A fresh folder inside the package's .tmp/ (CLAUDE.md: test files stay in the project).
function tempDir(t) {
  const base = new URL('../.tmp/', import.meta.url).pathname
  mkdirSync(base, { recursive: true })
  const dir = mkdtempSync(join(base, 'photos-'))
  t.after(() => rmSync(dir, { recursive: true, force: true }))
  return dir
}

const settled = () => new Promise((resolve) => setTimeout(resolve, 50))

test('jpegSize reads the frame header', () => {
  assert.deepEqual(jpegSize(jpegHeader(4608, 2592)), { width: 4608, height: 2592 })
  assert.deepEqual(jpegSize(jpegHeader(1280, 720, 0xc2)), { width: 1280, height: 720 }) // progressive
})

test('jpegSize gives null for anything else', () => {
  assert.equal(jpegSize(Buffer.from('not a jpeg')), null)
  assert.equal(jpegSize(Buffer.from([0xff, 0xd8, 0xff, 0xd9])), null) // no frame header
  assert.equal(jpegSize(jpegHeader(64, 48).subarray(0, 16)), null) // cut off
})

test('photo names are the UTC time they were taken', () => {
  assert.equal(photoName(Date.UTC(2026, 9, 24, 22, 11, 20, 413) / 1000), 'prowl-20261024-221120-413')
})

test('a new photo is listed and saved, and the same still twice is saved once', async (t) => {
  const dir = tempDir(t)
  const store = new PhotoStore({ dir, maxPhotos: 10, log: quiet })
  const t1 = Date.UTC(2026, 9, 24, 22, 11, 20, 413) / 1000
  const entry = store.add(jpegHeader(), t1)
  assert.deepEqual(entry, {
    name: 'prowl-20261024-221120-413',
    t: t1,
    width: 640,
    height: 480,
    bytes: jpegHeader().length,
    url: '/api/camera/photos/prowl-20261024-221120-413.jpg',
  })
  assert.deepEqual(store.read(entry.name), jpegHeader()) // readable while it's still being written
  store.add(jpegHeader(), t1)
  await settled()
  assert.deepEqual(readdirSync(dir), ['prowl-20261024-221120-413.jpg'])
  assert.deepEqual(store.find(t1), entry)
  assert.equal(store.find(t1 + 1), null)
})

test('photos already in the folder are found again, newest first, and other files are ignored', async (t) => {
  const dir = tempDir(t)
  writeFileSync(join(dir, 'prowl-20261024-100000-000.jpg'), jpegHeader(100, 50))
  writeFileSync(join(dir, 'prowl-20261024-120000-000.jpg'), jpegHeader(200, 100))
  writeFileSync(join(dir, 'notes.txt'), 'not a photo')
  writeFileSync(join(dir, 'holiday.jpg'), jpegHeader())
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
  const names = [0, 1, 2].map((i) => store.add(jpegHeader(), base + i).name)
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
