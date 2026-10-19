import test from 'node:test';
import assert from 'node:assert/strict';
import { defaults, initialHosts, loadState, recommendedBitrate, validateAddress, validateSettings } from './model.js';

test('manual addresses accept LAN, Tailscale, IPv6 and host names without accepting URLs', () => {
  for (const address of ['192.168.1.10', '100.64.0.10:47989', 'sunshine.local', 'my-pc', '::1', '[fd7a:115c:a1e0::1]:47989']) assert.equal(validateAddress(address), true, address);
  for (const address of ['', '999.1.1.1', '1.2.3', 'https://192.168.1.1', 'host/path', 'host:0', 'host:65536', 'two hosts', '[invalid]']) assert.equal(validateAddress(address), false, address);
});

test('prototype storage keeps review notes, decisions and per-host app choices', () => {
  const stored = { version: 1, hosts: initialHosts, settings: { ...defaults, bitrate: 42 }, decisions: { stretch: 'remove', osk: 'keep' }, notes: '請保留手把導航', appPreferences: { work: { desktop: { hidden: false, direct: true } } } };
  const restored = loadState({ getItem: () => JSON.stringify(stored) });
  assert.equal(restored.settings.bitrate, 42);
  assert.equal(restored.notes, stored.notes);
  assert.equal(restored.appPreferences.work.desktop.direct, true);
  assert.equal(restored.decisions.stretch, 'remove');
  assert.deepEqual(loadState({ getItem: () => '{broken' }), {});
  assert.deepEqual(loadState({ getItem: () => '{"version":999}' }), {});
});

test('invalid custom video settings do not become saved settings', () => {
  assert.equal(validateSettings(defaults), '');
  for (const invalid of [{ resolution: 'custom', customWidth: 0 }, { fps: 'custom', customFps: 0 }, { bitrate: '' }, { fec: 'fixed', fecPercent: 101 }, { hdr: true, hdrBrightnessMode: 'manual', hdrFullFrame: 1200 }, { hdr: true, hdrBrightnessMode: 'manual', hdrMin: -1 }]) assert.notEqual(validateSettings({ ...defaults, ...invalid }), '');
  assert.equal(validateSettings({ ...defaults, hdr: true, hdrBrightnessMode: 'manual', hdrMin: 0.0001, hdrFullFrame: 400, hdrPeak: 1000 }), '');
});

test('recommended bitrate follows existing native resolution and FPS policy', () => {
  assert.equal(recommendedBitrate(defaults), 20);
  assert.equal(recommendedBitrate({ ...defaults, resolution: '1280x720', fps: '30' }), 5);
  assert.equal(recommendedBitrate({ ...defaults, resolution: '3840x2160', fps: '60' }), 80);
  assert.equal(recommendedBitrate({ ...defaults, fps: '120' }), 28);
  assert.equal(recommendedBitrate({ ...defaults, yuv444: true }), 40);
});
