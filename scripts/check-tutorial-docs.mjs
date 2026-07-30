import { existsSync, readFileSync } from 'node:fs'
import { dirname, extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const docsRoot = join(repositoryRoot, 'docs')

const corePages = [
  {
    zh: 'docs/tutorials/01-quickstart/quickstart.md',
    en: 'docs/en/tutorials/01-quickstart/quickstart.md',
    zhRoute: '/tutorials/01-quickstart/quickstart',
    enRoute: '/en/tutorials/01-quickstart/quickstart',
    zhTitle: '快速开始：部署、登录与首次检测',
    enTitle: 'Quick Start: Deployment, Sign-In, and First Detection'
  },
  {
    zh: 'docs/tutorials/02-scenario-config/scenario-config.md',
    en: 'docs/en/tutorials/02-scenario-config/scenario-config.md',
    zhRoute: '/tutorials/02-scenario-config/scenario-config',
    enRoute: '/en/tutorials/02-scenario-config/scenario-config',
    zhTitle: '场景任务配置：通道、区域、参数与告警',
    enTitle: 'Scenario Task Configuration: Channels, Regions, Parameters, and Alarms'
  },
  {
    zh: 'docs/tutorials/03-vlm-guide/vlm-guide.md',
    en: 'docs/en/tutorials/03-vlm-guide/vlm-guide.md',
    zhRoute: '/tutorials/03-vlm-guide/vlm-guide',
    enRoute: '/en/tutorials/03-vlm-guide/vlm-guide',
    zhTitle: 'VLM 与 DINO：提示词驱动的视觉任务',
    enTitle: 'VLM and DINO: Prompt-Driven Vision Tasks'
  },
  {
    zh: 'docs/tutorials/04-pipeline-orchestration/pipeline-orchestration.md',
    en: 'docs/en/tutorials/04-pipeline-orchestration/pipeline-orchestration.md',
    zhRoute: '/tutorials/04-pipeline-orchestration/pipeline-orchestration',
    enRoute: '/en/tutorials/04-pipeline-orchestration/pipeline-orchestration',
    zhTitle: '算法编排：修改与创建 Pipeline',
    enTitle: 'Pipeline Orchestration: Modify and Create Pipelines'
  },
  {
    zh: 'docs/tutorials/05-model-porting/model-porting.md',
    en: 'docs/en/tutorials/05-model-porting/model-porting.md',
    zhRoute: '/tutorials/05-model-porting/model-porting',
    enRoute: '/en/tutorials/05-model-porting/model-porting',
    zhTitle: '第三方模型接入：转换、上传与验证',
    enTitle: 'Third-Party Model Integration: Convert, Upload, and Validate'
  }
]

const indexes = [
  {
    path: 'docs/tutorials/index.md',
    title: 'CosmoEdge 系统使用指南',
    language: 'zh'
  },
  {
    path: 'docs/en/tutorials/index.md',
    title: 'Using CosmoEdge',
    language: 'en'
  }
]

const placeholders = [
  { pattern: /待填写/u, label: '待填写' },
  { pattern: /待补充/u, label: '待补充' },
  { pattern: /待完善/u, label: '待完善' },
  { pattern: /\bTODO\b/u, label: 'TODO' },
  { pattern: /\bTBD\b/u, label: 'TBD' },
  { pattern: /\bComing soon\b/iu, label: 'Coming soon' },
  { pattern: /\bDraft for review\b/iu, label: 'Draft for review' }
]

const editorialResidues = [
  { pattern: /卷[一二三四五]/u, label: 'volume-style Chinese title' },
  { pattern: /\bVolume\s+[1-5]\b/iu, label: 'volume-style English title' },
  { pattern: /事间中心/u, label: 'misspelling 事间中心' }
]

const requiredIntro = {
  zh: ['适合谁', '完成后能做什么', '使用前提', '预计时间', '是否需要设备', '最终验收结果'],
  en: [
    'Who this is for',
    'What you will accomplish',
    'Prerequisites',
    'Estimated time',
    'Device required',
    'Final acceptance result'
  ]
}

const failures = []

function fail(file, message) {
  failures.push(`${file}: ${message}`)
}

function removeFrontmatter(markdown) {
  const match = markdown.match(/^---\r?\n[\s\S]*?\r?\n---(?:\r?\n|$)/u)
  return match ? markdown.slice(match[0].length) : markdown
}

function stripNonProse(markdown) {
  return removeFrontmatter(markdown)
    .replace(/```[\s\S]*?```/gu, '')
    .replace(/~~~[\s\S]*?~~~/gu, '')
    .replace(/<!--[\s\S]*?-->/gu, '')
}

function frontmatterTitle(markdown) {
  const match = markdown.match(/^---\r?\n([\s\S]*?)\r?\n---(?:\r?\n|$)/u)
  if (!match) return null
  const titleLine = match[1].match(/^title:\s*(.+?)\s*$/mu)
  if (!titleLine) return null
  return titleLine[1].replace(/^(['"])(.*)\1$/u, '$2')
}

function linkTarget(rawTarget) {
  let target = rawTarget.trim()
  if (target.startsWith('<') && target.endsWith('>')) {
    target = target.slice(1, -1)
  } else {
    const optionalTitle = target.match(/^(\S+)(?:\s+["'(].*)$/u)
    if (optionalTitle) target = optionalTitle[1]
  }
  return decodeURIComponent(target.split('#', 1)[0].split('?', 1)[0])
}

function resolveLocalTarget(sourcePath, rawTarget, isImage) {
  const target = linkTarget(rawTarget)
  if (!target || target.startsWith('#')) return null
  if (/^(?:https?:|mailto:|tel:|data:)/iu.test(target)) return null

  let absolute
  if (target.startsWith('/')) {
    const sitePath = target.replace(/^\/cosmo-edge\/?/u, '/').replace(/^\//u, '')
    absolute = join(docsRoot, sitePath)
  } else {
    absolute = resolve(dirname(join(repositoryRoot, sourcePath)), target)
  }

  if (isImage || extname(absolute)) return [normalize(absolute)]
  return [normalize(absolute), normalize(`${absolute}.md`), normalize(join(absolute, 'index.md'))]
}

function checkLinksAndImages(file, markdown) {
  const imagePattern = /!\[([^\]]*)\]\(([^)]+)\)/gu
  for (const match of markdown.matchAll(imagePattern)) {
    const alt = match[1].trim()
    if (!alt) fail(file, `image ${match[2]} has an empty alt description`)

    const candidates = resolveLocalTarget(file, match[2], true)
    if (candidates && !candidates.some((candidate) => existsSync(candidate))) {
      fail(file, `image target does not exist: ${match[2]}`)
    }
  }

  const withoutImages = markdown.replace(imagePattern, '')
  const linkPattern = /(?<!!)\[[^\]]+\]\(([^)]+)\)/gu
  for (const match of withoutImages.matchAll(linkPattern)) {
    const candidates = resolveLocalTarget(file, match[1], false)
    if (candidates && !candidates.some((candidate) => existsSync(candidate))) {
      fail(file, `link target does not exist: ${match[1]}`)
    }
  }
}

function checkPage({ path: file, title, language, isCore }) {
  const absolute = join(repositoryRoot, file)
  if (!existsSync(absolute)) {
    fail(file, 'required page is missing')
    return
  }

  const markdown = readFileSync(absolute, 'utf8')
  if (!markdown.startsWith('---')) fail(file, 'frontmatter must start at byte 0')
  if (frontmatterTitle(markdown) !== title) fail(file, `frontmatter title must be "${title}"`)

  const prose = stripNonProse(markdown)
  const h1 = [...prose.matchAll(/^#\s+(.+?)\s*$/gmu)]
  if (h1.length !== 1) {
    fail(file, `expected exactly one H1 outside code blocks, found ${h1.length}`)
  } else if (h1[0][1] !== title) {
    fail(file, `H1 must be "${title}"`)
  }

  for (const placeholder of placeholders) {
    if (placeholder.pattern.test(prose)) fail(file, `contains forbidden placeholder "${placeholder.label}"`)
  }
  for (const residue of editorialResidues) {
    if (residue.pattern.test(prose)) fail(file, `contains ${residue.label}`)
  }

  if (/<!--[\s\S]*?(?:OCR|ocr|截图|screenshot)[\s\S]*?-->/u.test(markdown)) {
    fail(file, 'contains an OCR or screenshot editorial HTML comment')
  }

  if (isCore) {
    for (const label of requiredIntro[language]) {
      if (!prose.includes(label)) fail(file, `missing introduction field "${label}"`)
    }
  }

  checkLinksAndImages(file, markdown)
}

for (const pair of corePages) {
  checkPage({ path: pair.zh, title: pair.zhTitle, language: 'zh', isCore: true })
  checkPage({ path: pair.en, title: pair.enTitle, language: 'en', isCore: true })
}

for (const index of indexes) checkPage({ ...index, isCore: false })

const configPath = join(docsRoot, '.vitepress', 'config.mts')
const config = readFileSync(configPath, 'utf8')
for (const required of [
  "text: '系统使用'",
  "text: 'Using CosmoEdge'",
  "text: '基础使用'",
  "text: 'AI 能力配置'",
  "text: '高级扩展'",
  "text: 'Basic Use'",
  "text: 'AI Capability Configuration'",
  "text: 'Advanced Extensions'"
]) {
  if (!config.includes(required)) fail('docs/.vitepress/config.mts', `missing navigation entry ${required}`)
}
for (const page of corePages) {
  for (const [title, route] of [
    [page.zhTitle, page.zhRoute],
    [page.enTitle, page.enRoute]
  ]) {
    if (!config.includes(`text: '${title}'`) || !config.includes(`link: '${route}'`)) {
      fail('docs/.vitepress/config.mts', `missing stable navigation mapping "${title}" -> "${route}"`)
    }
  }
}

if (failures.length > 0) {
  console.error(`Tutorial documentation check failed with ${failures.length} issue(s):`)
  for (const failure of failures) console.error(`- ${failure}`)
  process.exit(1)
}

console.log(
  `Tutorial documentation check passed: ${corePages.length * 2} core pages, ${indexes.length} indexes, bilingual pairs, links, images, and navigation.`
)
