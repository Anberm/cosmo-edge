import assert from 'node:assert/strict'

import { isSupportedUpgradePackageName } from '../src/utils/upgradePackage.js'

const accepted = [
  'cosmo-V1.0.0-0123456789abcdef0123456789abcdef.tar.gz',
  'cosmo-v1.0.0-0123456789ABCDEF0123456789ABCDEF.tar.gz'
]

const rejected = [
  '',
  'cosmo-V1.0.0.tar.gz',
  'cosmo-V1.0.0-0123456789abcdef0123456789abcde.tar.gz',
  'cosmo-release-a.tar.gz',
  'cosmo-release-Ab.tar.gz',
  'cosmo-release-../escape.tar.gz',
  `cosmo-release-${'a'.repeat(65)}.tar.gz`
]

for (const name of accepted) {
  assert.equal(isSupportedUpgradePackageName(name), true, `expected accepted: ${name}`)
}
for (const name of rejected) {
  assert.equal(isSupportedUpgradePackageName(name), false, `expected rejected: ${name}`)
}
