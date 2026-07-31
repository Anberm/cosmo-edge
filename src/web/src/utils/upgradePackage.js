const legacyUpgradePackagePattern =
  /^cosmo-[Vv]\d+\.\d+\.\d+-[0-9a-fA-F]{32}\.tar\.gz$/
const signedReleasePackagePattern =
  /^cosmo-release-[a-z0-9][a-z0-9._-]{0,63}\.tar\.gz$/

export const isSupportedUpgradePackageName = name =>
  typeof name === 'string' &&
  (legacyUpgradePackagePattern.test(name) ||
    signedReleasePackagePattern.test(name))
