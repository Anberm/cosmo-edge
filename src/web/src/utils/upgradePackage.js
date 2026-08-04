const upgradePackagePattern =
  /^cosmo-[Vv]\d+\.\d+\.\d+-[0-9a-fA-F]{32}\.tar\.gz$/

export const isSupportedUpgradePackageName = name =>
  typeof name === 'string' && upgradePackagePattern.test(name)
